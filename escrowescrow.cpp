/*
Copyright 2019 cc32d9@gmail.com
Copyright 2019 EOS Geneva

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/action.hpp>
#include <eosio/transaction.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/time.hpp>

#include "escrowescrow_constants.hpp"

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
  

  ACTION setarbiter(name account, string contact_name, string email, string description,
                    string website, string phone, string iso_country)
  {
    require_auth(account);
    check(contact_name.length() > 0, "Contact name cannot be empty");
    check(email.length() > 0, "Email cannot be empty");
    if( iso_country.length() > 0 ) {
      check(iso_country.length() == 2, "ISO country code must be 2 letters");
      for( int i = 0; i < iso_country.length(); i++ ) {
        char c = iso_country[i];
        check('A' <= c && c <= 'Z', "Invalid character in ISO country code");
      }
    }

    auto setter = [&]( auto& item ) {
      item.account = account;
      item.contact_name = contact_name;
      item.email = email;
      item.description = description;
      item.website = website;
      item.phone = phone;
      item.iso_country = iso_country;
      item.is_active = 1;
    };
          
    arbiters _arbiters(_self, _self.value);
    auto arbitr = _arbiters.find(account.value);
    if( arbitr == _arbiters.end() ) {
      _arbiters.emplace(account, setter);
    }
    else {
      _arbiters.modify(*arbitr, account, setter);
    }
  }

  
  ACTION delarbiter(name account)
  {
    require_auth(account);
    arbiters _arbiters(_self, _self.value);
    auto arbitr = _arbiters.find(account.value);
    check(arbitr != _arbiters.end(), "Cannot find the arbiter");
    check(arbitr->is_active, "This arbiter is already marked for deletion");
    _arbiters.modify(*arbitr, account, [&]( auto& item ) {
        item.is_active = 0;
      });
  }

  
  ACTION newdeal(name creator, string description, name tkcontract, asset& quantity,
                 name buyer, name seller, name arbiter, uint32_t days)
  {
    require_auth(creator);
    check(description.length() > 0, "description cannot be empty");
    check(is_account(tkcontract), "tkcontract account does not exist");
    check(quantity.is_valid(), "invalid quantity" );
    check(quantity.amount > 0, "must specify a positive quantity" );
    check(is_account(buyer), "buyer account does not exist");
    check(is_account(seller), "seller account does not exist");
    check(is_account(arbiter), "arbiter account does not exist");
    check(buyer != seller && buyer != arbiter && seller != arbiter,
                 "Buyer, seller and arbiter must be different accounts");
    
    check(days > 0, "delivery term should be a positive number of days");

    // Validate the token contract. The buyer should have a non-zero balance of payment token
    accounts token_accounts(tkcontract, buyer.value);
    const auto token_name = quantity.symbol.code().raw();
    auto token_accounts_itr = token_accounts.find(token_name);
    check(token_accounts_itr != token_accounts.end() && token_accounts_itr->balance.amount > 0,
                 "Invalid currency or the buyer has no funds");

    // Check that arbiter is active
    arbiters _arbiters(_self, _self.value);
    auto arb = _arbiters.get(arbiter.value, "Cannot find the arbiter");
    check(arb.is_active, "This arbiter marked as inactive");
    
    // deal ID is first 32 bits from transaction ID
    uint64_t id = 0;
    auto size = transaction_size();
    char buf[size];
    uint32_t read = read_transaction( buf, size );
    check( size == read, "read_transaction failed");
    checksum256 h = sha256(buf, size);
    auto hbytes = h.extract_as_byte_array();
    for(int i=0; i<4; i++) {
      id <<=8;
      id |= hbytes[i];
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
        d.expires = time_point_sec(current_time_point()) + NEW_DEAL_EXPIRES;
        d.flags = 0;
        if( creator == buyer ) {
          d.flags |= BUYER_ACCEPTED_FLAG;
        } else if ( creator == seller ) {
          d.flags |= SELLER_ACCEPTED_FLAG;
        }
      });
    _notify(name("new"), "New deal created", *idx);
    
    require_recipient(buyer);
    require_recipient(seller);
    require_recipient(arbiter);
    _clean_expired_deals(id);
  }

  

  ACTION accept(name party, uint64_t deal_id)
  {
    require_auth(party);
    auto dealitr = _deals.find(deal_id);
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;
    auto flags = d.flags;

    check(d.expires > time_point_sec(current_time_point()), "The deal is expired");
    
    if( party == d.buyer ) {
      check( (d.flags & BUYER_ACCEPTED_FLAG) == 0, "Buyer has already accepted this deal");
      flags |= BUYER_ACCEPTED_FLAG;
    } else if (party == d.seller ) {
      check( (d.flags & SELLER_ACCEPTED_FLAG) == 0, "Seller has already accepted this deal");
      flags |= SELLER_ACCEPTED_FLAG;
    } else {
      check(false, "Deal can only be accepted by either seller or buyer");
    }
      
    if( (flags & BOTH_ACCEPTED_FLAG) == BOTH_ACCEPTED_FLAG ) {
      _deals.modify( *dealitr, party, [&]( auto& item ) {
          item.flags = flags;
          item.expires = time_point_sec(current_time_point()) + ACCEPTED_DEAL_EXPIRES;
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
  [[eosio::on_notify("*::transfer")]]
    void transfer_handler (name from, name to, asset quantity, string memo) {
    if(to == _self) {
      check(memo.length() > 0, "Memo must contain a valid deal ID");
      
      uint64_t deal_id = 0;
      for( int i = 0; i < memo.length(); i++ ) {
        char c = memo[i];
        check('0' <= c && c <= '9', "Invalid character in memo. Expected only digits");
        deal_id *= 10;
        deal_id += (c - '0');
      }
      
      auto dealitr = _deals.find(deal_id);
      check(dealitr != _deals.end(), (string("Cannot find deal ID: ") + to_string(deal_id)).c_str());
      const deal& d = *dealitr;

      check(d.expires > time_point_sec(current_time_point()), "The deal is expired");

      check((d.flags & DEAL_FUNDED_FLAG) == 0, "The deal is already funded");
      check((d.flags & BOTH_ACCEPTED_FLAG) == BOTH_ACCEPTED_FLAG,
                   "The deal is not accepted yet by both parties");
      check(from == d.buyer, "The deal can only funded by buyer");      
      const extended_asset payment(quantity, name{get_first_receiver()});

      check(payment == d.price,
                   (string("Invalid amount or currency. Expected ") +
                    d.price.quantity.to_string() + " via " + d.price.contract.to_string()).c_str());
      _deals.modify( *dealitr, _self, [&]( auto& item ) {
          item.funded = time_point_sec(current_time_point());
          item.expires = item.funded + (item.days * DAY_SEC);
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
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check((d.flags & DEAL_ARBITRATION_FLAG) == 0, "The deal is in arbitration");
    check(d.expires > time_point_sec(current_time_point()), "The deal is expired");
    
    if( (d.flags & DEAL_FUNDED_FLAG) == 0 ) {
      // not funded, so any of the parties can cancel the deal
      check(has_auth(d.buyer) || has_auth(d.seller),
                   "Only seller or buyer can cancel the deal");
    }
    else {
      check((d.flags & DEAL_DELIVERED_FLAG) == 0, "The deal is already delivered, cannot cancel");
      // funded, so only seller can cancel the deal
      check(has_auth(d.seller), "The deal is funded, so only seller can cancel it");
      _send_payment(d.buyer, d.price,
                    string("Deal ") + to_string(d.id) + ": canceled by seller");
      _notify(name("refunded"), "Deal canceled by seller, buyer got refunded", d);
    }
    
    _notify(name("canceled"), "The deal is canceled", d);    
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }

  
  
  ACTION delivered(uint64_t deal_id, string memo)
  {
    auto dealitr = _deals.find(deal_id);
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check(d.expires > time_point_sec(current_time_point()), "The deal is expired");
    
    check((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    check((d.flags & DEAL_DELIVERED_FLAG) == 0, "The deal is already marked as delivered");
    check(has_auth(d.seller), "Only seller can mark a deal as delivered");

    _deals.modify( *dealitr, d.seller, [&]( auto& item ) {
        item.expires = time_point_sec(current_time_point()) + DELIVERED_DEAL_EXPIRES;
        item.flags |= DEAL_DELIVERED_FLAG;
        item.delivery_memo = memo;
      });

    _notify(name("delivered"), "Deal is marked as delivered", d);
    require_recipient(d.buyer);
    _clean_expired_deals(deal_id);
  }


  // Goods Received may be made before "delivered", but the deal must be funded first
  ACTION goodsrcvd(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    check(has_auth(d.buyer), "Only buyer can sign-off Goods Received");

    _send_payment(d.seller, d.price,
                  string("Deal ") + to_string(d.id) + ": goods received, deal closed");
    _notify(name("closed"), "Goods received, deal closed", d);
    if( d.flags & DEAL_ARBITRATION_FLAG ) {
      require_recipient(d.arbiter);
    }
    _deals.erase(dealitr);    
    _clean_expired_deals(deal_id);
  }


  
  ACTION extend(uint64_t deal_id, uint32_t moredays)
  {
    auto dealitr = _deals.find(deal_id);
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check(d.expires > time_point_sec(current_time_point()), "The deal is expired");

    check((d.flags & DEAL_FUNDED_FLAG), "The deal is not funded yet");
    check(has_auth(d.buyer), "Only buyer can extend a deal");

    _deals.modify( *dealitr, _self, [&]( auto& item ) {
        item.days += moredays;
        item.expires = item.funded + (item.days * DAY_SEC);
        });

    _notify(name("extended"), "Deal extended by " + to_string(moredays) + " more days", d);
    require_recipient(d.seller);
    _clean_expired_deals(deal_id);
  }



  ACTION arbrefund(uint64_t deal_id)
  {
    auto dealitr = _deals.find(deal_id);
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check((d.flags & DEAL_ARBITRATION_FLAG), "The deal is not open for arbitration");
    require_auth(d.arbiter);

    arbiters _arbiters(_self, _self.value);
    auto arbitr = _arbiters.find(d.arbiter.value);
    check(arbitr != _arbiters.end(), "Cannot find the arbiter");
    _arbiters.modify(*arbitr, _self, [&]( auto& item ) {
        item.processed_deals++;
      });
    
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
    check(dealitr != _deals.end(), "Cannot find deal_id");
    const deal& d = *dealitr;

    check((d.flags & DEAL_ARBITRATION_FLAG), "The deal is not open for arbitration");
    require_auth(d.arbiter);

    arbiters _arbiters(_self, _self.value);
    auto arbitr = _arbiters.find(d.arbiter.value);
    check(arbitr != _arbiters.end(), "Cannot find the arbiter");
    _arbiters.modify(*arbitr, _self, [&]( auto& item ) {
        item.processed_deals++;
      });
    
    _send_payment(d.seller, d.price,
                  string("Deal ") + to_string(d.id) + ": enforced by arbitration");
    _notify(name("arbenforce"), "Deal enforced by arbitration, seller got paid", d);
    require_recipient(d.buyer);
    _deals.erase(dealitr);
    _clean_expired_deals(deal_id);
  }

  

  // erase up to X expired deals and one arbiter
  ACTION wipeexpired(uint16_t count)
  {
    bool done_something = false;
    auto _now = time_point_sec(current_time_point());
    auto dealidx = _deals.get_index<name("expires")>();
    auto dealitr = dealidx.lower_bound(1); // 0 is for deals locked for arbitration
    while( count-- > 0 && dealitr != dealidx.end() && dealitr->expires <= _now ) {
      _deal_expired(*dealitr);
      dealitr = dealidx.lower_bound(1);
      done_something = true;
    }

    arbiters _arbiters(_self, _self.value);
    auto arbidx = _arbiters.get_index<name("active")>();
    auto arbitr = arbidx.lower_bound(0);
    bool deleted = false;
    while( !deleted && arbitr != arbidx.end() && arbitr->is_active == 0 ) {
      name arbiter = arbitr->account;
      auto adidx = _deals.get_index<name("arbiters")>();
      auto aditr = adidx.lower_bound(arbiter.value);
      if( aditr == adidx.end() || aditr->arbiter != arbiter ) {
        // no open deals for this arbiter. deleting.
        arbidx.erase(arbitr);
        deleted = true;
        // leave trace
        action(
               permission_level{_self, name("active")},
               _self, 
               name("arbdeleted"), 
               std::make_tuple(arbiter)
               ).send();        
        done_something = true;
      }
      else {
        arbitr++;
      }
    }      

    check(done_something, "There are no expired transactions or inactive arbiters");
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
    string      delivery_memo;
  };

  ACTION notify(name deal_status, string message, uint64_t deal_id, name created_by,
                string description, name tkcontract, asset& quantity,
                name buyer, name seller, name arbiter, uint32_t days, string delivery_memo)
  {
    require_auth(_self);
  }


  ACTION arbdeleted(name arbiter) {
    require_auth(_self);
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
    string         delivery_memo;
    auto primary_key()const { return id; }
    uint64_t get_expires()const { return expires.utc_seconds; }
    uint64_t get_arbiter()const { return arbiter.value; }
  };

  typedef eosio::multi_index<
    name("deals"), deal,
    indexed_by<name("expires"), const_mem_fun<deal, uint64_t, &deal::get_expires>>,
    indexed_by<name("arbiters"), const_mem_fun<deal, uint64_t, &deal::get_arbiter>>> deals;

  deals _deals;


  struct [[eosio::table("arbiters")]] arbiter {
    name           account;
    string         contact_name;
    string         email;
    string         description;
    string         website;
    string         phone;
    string         iso_country;
    uint32_t       processed_deals = 0;
    uint8_t        is_active;
    
    auto primary_key()const { return account.value; }
    uint64_t get_is_active()const { return is_active; }
  };

  typedef eosio::multi_index<
    name("arbiters"), arbiter,
    indexed_by<name("active"), const_mem_fun<arbiter, uint64_t, &arbiter::get_is_active>>> arbiters;

      
  void _clean_expired_deals(uint64_t senderid)
  {
    auto _now = time_point_sec(current_time_point());
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
              "Goods Received was not issued on time. The deal is open for arbitration", d);
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
        .deal_id=d.id, .created_by=d.created_by, .description=d.description,
        .tkcontract=d.price.contract, .quantity=d.price.quantity,
        .buyer=d.buyer, .seller=d.seller, .arbiter=d.arbiter, .days=d.days,
        .delivery_memo=d.delivery_memo }
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

  
