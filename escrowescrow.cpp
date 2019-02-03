#include <eosiolib/eosio.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/time.hpp>
#include <eosiolib/transaction.hpp>

using namespace eosio;

using std::string;
using std::to_string;

CONTRACT escrowescrow : public eosio::contract {
 public:
  escrowescrow( name self, name code, datastream<const char*> ds ):
    contract(self, code, ds),
    _deals(self, self.value)
      {}

  const uint16_t BUYER_ACCEPTED_FLAG    = 1 << 0;
  const uint16_t SELLER_ACCEPTED_FLAG   = 1 << 1;
  const uint16_t DEAL_FUNDED_FLAG       = 1 << 2;
  const uint16_t DEAL_DELIVERED_FLAG    = 1 << 3;
  const uint16_t DEAL_ARBITRATION_FLAG  = 1 << 4;

  const uint16_t BOTH_ACCEPTED_FLAG = BUYER_ACCEPTED_FLAG | SELLER_ACCEPTED_FLAG;
  
  const int WIPE_EXP_DEALS_TAX = 3; // delete up to so many expired deals in every action
  const unsigned int WIPE_EXP_TX_DELAY = 10; // deferred transaction delay, seconds
  
  const int NEW_DEAL_EXPIRES = 3*3600*24;
  const int ACCEPTED_DEAL_EXPIRES = 3*3600*24;
  const int DELIVERED_DEAL_EXPIRES = 3*3600*24;



    
  
  ACTION newdeal(name creator, string description, name tkcontract, asset& quantity,
                 name buyer, name seller, name arbiter, uint32_t days)
  {
    require_auth(creator);
    eosio_assert(description.length() > 0, "description cannot be empty");
    eosio_assert(is_account(tkcontract), "tkcontract account does not exist");
    eosio_assert(quantity.is_valid(), "invalid quantity" );
    eosio_assert(quantity.amount > 0, "must specify a positive quantity" );
    eosio_assert(is_account(buyer), "buyer account does not exist");
    eosio_assert(is_account(seller), "seller account does not exist");
    eosio_assert(is_account(arbiter), "arbiter account does not exist");
    eosio_assert(buyer != seller && buyer != arbiter && seller != arbiter,
                 "Buyer, seller and arbiter must be different accounts");
    
    eosio_assert(days > 0, "delivery term should be a positive number of days");

    // Validate the token contract. The buyer should have a non-zero balance of payment token
    accounts token_accounts(tkcontract, buyer.value);
    const auto token_name = quantity.symbol.raw();
    auto token_accounts_itr = token_accounts.find(token_name);
    eosio_assert(token_accounts_itr != token_accounts.end(),
                 "Invalid token contract or the buyer has no funds");

    // deal ID is first 32 bits from transaction ID
    uint64_t id = 0;
    auto size = transaction_size();
    char buf[size];
    uint32_t read = read_transaction( buf, size );
    eosio_assert( size == read, "read_transaction failed");
    capi_checksum256 h;
    sha256(buf, read, &h);
    for(int i=0; i<4; i++) {
      id <<=8;
      id |= h.hash[i];
    }

    auto idx = _deals.emplace(creator, [&]( auto& d ) {
        d.id = id;
        d.created_by = creator;
        d.description = description;
        d.price.contract = tkcontract;
        d.price.quantity = quantity;
        d.buyer = buyer;
        d.seller = seller;
        d.arbiter = arbiter;
        d.days = days;
        d.expires = time_point_sec(now()) + NEW_DEAL_EXPIRES;
        d.flags = 0;
        if( creator == buyer ) {
          d.flags |= BUYER_ACCEPTED_FLAG;
        } else if ( creator == seller ) {
          d.flags |= SELLER_ACCEPTED_FLAG;
        }
      });
    _notify(name("new"), "New deal created", *idx);
    
    require_recipient(seller);
    require_recipient(buyer);
    _clean_expired_deals(id);
  }

  

  ACTION accept(name party, uint64_t deal_id)
  {
    require_auth(party);
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;
    auto flags = d.flags;
    
    if( party == d.buyer ) {
      eosio_assert( (d.flags & BUYER_ACCEPTED_FLAG) == 0, "Buyer has already accepted this deal");
      flags |= BUYER_ACCEPTED_FLAG;
    } else if (party == d.seller ) {
      eosio_assert( (d.flags & SELLER_ACCEPTED_FLAG) == 0, "Seller has already accepted this deal");
      flags |= SELLER_ACCEPTED_FLAG;
    } else {
      eosio_assert(false, "Deal can only be accepted by either seller or buyer");
    }
      
    if( (flags & BOTH_ACCEPTED_FLAG) == BOTH_ACCEPTED_FLAG ) {
      _deals.modify( *dealitr, party, [&]( auto& item ) {
          item.flags = flags;
          item.expires = time_point_sec(now()) + ACCEPTED_DEAL_EXPIRES;
        });
      _notify(name("accepted"), "Deal is fully accepted", d);
      require_recipient(d.seller);
      require_recipient(d.buyer);
    }
    else {
      _deals.modify( *dealitr, party, [&]( auto& item ) {
          item.flags = flags;
        });
    }

    _clean_expired_deals(deal_id);
  }


  
  // Accept funds for a deal
  void transfer_handler (name from, name to, asset quantity, string memo)
  {
    if(to == _self) {
      eosio_assert(memo.length() > 0, "Memo must contain a valid deal ID");
      
      int deal_id = std::stoi(memo, nullptr, 10);
      auto dealitr = _deals.find(deal_id);
      eosio_assert(dealitr != _deals.end(), "Cannot find deal ID");
      const deal& d = *dealitr;

      eosio_assert((d.flags & DEAL_FUNDED_FLAG) == 0, "The deal is already funded");
      eosio_assert((d.flags & BOTH_ACCEPTED_FLAG) == BOTH_ACCEPTED_FLAG,
                   "The deal is not accepted yet by both parties");
      eosio_assert(from == d.buyer, "The deal can only funded by buyer");      
      const extended_asset payment(quantity, name{get_code()});

      eosio_assert(payment == d.price,
                   (string("Invalid amount or currency. Expected ") +
                    d.price.quantity.to_string() + " via " + d.price.contract.to_string()).c_str());
      _deals.modify( *dealitr, _self, [&]( auto& item ) {
          item.funded = time_point_sec(now());
          item.expires = item.funded + (item.days * 24 * 36000);
          item.flags |= DEAL_FUNDED_FLAG;
        });
      
      _notify(name("funded"), "Deal is funded", d);
      require_recipient(d.seller);
      _clean_expired_deals(deal_id);
    }
  }

  
  ACTION cancel(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    if( (d.flags & DEAL_FUNDED_FLAG) == 0 ) {
      // not funded, so any of the parties can cancel the deal
      eosio_assert(has_auth(d.buyer) || has_auth(d.seller),
                   "Only seller or buyer can cancel the deal");
    }
    else {
      // funded, so only seller can cancel the deal
      require_auth(d.seller);
      _send_payment(d.buyer, d.price,
                    string("Deal ") + to_string(d.id) + ": canceled by seller");
      _notify(name("refunded"), "Deal canceled by seller, buyer got refunded", d);
    }
    
    _notify(name("canceled"), "The deal is canceled", d);    
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }

  
  
  ACTION delivered(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    eosio_assert((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    eosio_assert((d.flags & DEAL_DELIVERED_FLAG) == 0, "The deal is already marked as delivered");
    require_auth(d.seller);

    _deals.modify( *dealitr, _self, [&]( auto& item ) {
        item.expires = time_point_sec(now()) + DELIVERED_DEAL_EXPIRES;
        item.flags |= DEAL_DELIVERED_FLAG;
      });

    _notify(name("delivered"), "Deal is marked as delivered", d);
    require_recipient(d.buyer);
    _clean_expired_deals(deal_id);
  }


  // Goods Received may be made before "delivered", but the deal must be funded first
  ACTION goodsrcvd(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    eosio_assert((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    require_auth(d.buyer);

    _send_payment(d.seller, d.price,
                  string("Deal ") + to_string(d.id) + ": goods received, deal closed");
    _notify(name("closed"), "Goods received, deal closed", d);    
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }


  
  ACTION extend(uint64_t deal_id, uint32_t moredays)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    eosio_assert((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    require_auth(d.buyer);

    _deals.modify( *dealitr, _self, [&]( auto& item ) {
        item.days += moredays;
        item.expires = item.funded + (item.days * 24 * 36000);
        });

    _notify(name("extended"), "Deal extended by " + to_string(moredays) + " more days", d);
    require_recipient(d.seller);
    _clean_expired_deals(deal_id);
  }



  ACTION arbrefund(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    eosio_assert((d.flags & DEAL_ARBITRATION_FLAG), "The deal is not open for arbitration");
    require_auth(d.arbiter);
    
    _send_payment(d.buyer, d.price,
                  string("Deal ") + to_string(d.id) + ": canceled by arbitration");
    _notify(name("arbrefund"), "Deal canceled by arbitration, buyer got refunded", d);
    require_recipient(d.seller);
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }
  

  
  ACTION arbenforce(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    eosio_assert(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    eosio_assert((d.flags & DEAL_ARBITRATION_FLAG), "The deal is not open for arbitration");
    require_auth(d.arbiter);
    
    _send_payment(d.seller, d.price,
                  string("Deal ") + to_string(d.id) + ": enforced by arbitration");
    _notify(name("arbenforce"), "Deal enforced by arbitration, seller got paid", d);
    require_recipient(d.buyer);
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }


  // erase up to X expired deals
  ACTION wipeexpired(uint16_t count)
  {
    auto _now = time_point_sec(now());
    auto dealidx = _deals.get_index<name("expires")>();
    auto dealitr = dealidx.lower_bound(1); // 0 is for deals locked for arbitration
    while( count-- > 0 && dealitr != dealidx.end() && dealitr->expires <= _now ) {
      _deal_expired(*dealitr);
      dealitr = dealidx.lower_bound(1);
    }
  }
      
      
  // inline notifications
  struct deal_notification_abi {
    name        deal_status;
    string      message;
    uint64_t    deal_id;
    name        created_by;
    string      description;
    name        tkcontract;
    asset       quantity;
    name        buyer;
    name        seller;
    name        arbiter;
    uint32_t    days;    
  };

  ACTION notify(name deal_status, string message, uint64_t deal_id, name created_by,
                string description, name tkcontract, asset& quantity,
                name buyer, name seller, name arbiter, uint32_t days)
  {
    require_auth(permission_level(_self, name("active")));
  }

  
 private:
  
  struct [[eosio::table("deals")]] deal {
    uint64_t       id;
    name           created_by;
    string         description;
    extended_asset price;
    name           buyer;
    name           seller;
    name           arbiter;
    uint32_t       days;
    time_point_sec funded;
    time_point_sec expires;
    uint16_t       flags;
    auto primary_key()const { return id; }
    uint64_t get_expires()const { return expires.utc_seconds; }
  };

  typedef eosio::multi_index<
    name("deals"), deal,
    indexed_by<name("expires"), const_mem_fun<deal, uint64_t, &deal::get_expires>>> deals;

  deals _deals;
  
  void _clean_expired_deals(uint64_t senderid)
  {
    auto _now = time_point_sec(now());
    auto dealidx = _deals.get_index<name("expires")>();
    auto dealitr = dealidx.lower_bound(1); // 0 is for deals locked for arbitration
    if( dealitr != dealidx.end() && dealitr->expires <= _now ) {
      // Send a deferred transaction that wipes up to WIPE_EXP_DEALS_TAX expired records
      transaction tx;
      tx.actions.emplace_back(
                              permission_level{_self, name("active")},
                              _self,
                              name("wipeexpired"),
                              std::make_tuple(WIPE_EXP_DEALS_TAX)
                              );
      tx.delay_sec = WIPE_EXP_TX_DELAY;
      tx.send(senderid, _self);
    }
  }


  void _deal_expired(const deal& d)
  {
    if( d.flags & DEAL_DELIVERED_FLAG ) {
      _deals.modify( d, _self, [&]( auto& item ) {
          item.expires.utc_seconds = 0;
          item.flags |= DEAL_ARBITRATION_FLAG;
      });
      _notify(name("arbitration"),
              "Did not receive Goods Received on time. The deal is open for arbitration", d);
      require_recipient(d.seller);
      require_recipient(d.buyer);
      require_recipient(d.arbiter);
    }
    else {
      string msg = string("Deal ") + to_string(d.id) + " expired";
      if( d.flags & DEAL_FUNDED_FLAG ) {
        _send_payment(d.buyer, d.price, msg); // refund the buyer
        _notify(name("refund"), string("Deal ") + to_string(d.id) + " refunded", d);
      }
      else {
        require_recipient(d.buyer);  // in case of a payback, the buyer is already notified
      }
      require_recipient(d.seller);
      _notify(name("expired"), msg, d);
      _deals.erase(d);
    }
  }


  
  // leave a trace in history
  void _notify(name deal_status, const string message, const deal& d)
  {
    action {
      permission_level{_self, name("active")},
      _self,
      name("notify"),
      deal_notification_abi {
        .deal_status=deal_status,
        .message=message,
        .deal_id=d.id, .description=d.description, .tkcontract=d.price.contract,
        .quantity=d.price.quantity,
        .buyer=d.buyer, .seller=d.seller, .arbiter=d.arbiter, .days=d.days }
    }.send();
  }
  

  void _send_payment(name recipient, const extended_asset& x, const string memo)
  {
    action
      {
        permission_level{_self, name("active")},
        x.contract,
        name("transfer"),
        transfer  {
          .from=_self, .to=recipient,
          .quantity=x.quantity, .memo=memo
        }
      }.send();
  }
  


  // eosio.token structs

  struct transfer
  {
    name         from;
    name         to;
    asset        quantity;
    string       memo;
  };

  struct account {
    asset    balance;    
    uint64_t primary_key() const { return balance.symbol.raw(); }
  };

  typedef eosio::multi_index<name("accounts"), account> accounts;
    
};

  
extern "C" void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  if( action == name("transfer").value ) {
    execute_action<escrowescrow>( eosio::name(receiver), eosio::name(code),
                                  &escrowescrow::transfer_handler );
  }
  else if( code == receiver ) {
    switch( action ) {
      EOSIO_DISPATCH_HELPER( escrowescrow,
                             (newdeal)(accept)(cancel)(delivered)(goodsrcvd)
                             (extend)(arbrefund)(arbenforce)(wipeexpired));
    }
  }
}
  
