# Universal EOS escrow contract

This is an escrow service that anyone in EOS network can use. The
workflow is as follows:

Alice wants to buy pumpkins from Bob and she pays with FARMER token.

They trust Chris to be the arbiter in case of a conflict.

Alice or Bob create a deal in `escrowescrow`, by sending an action
`newdeal` with the following attributes:

* Text description of a deal
* Currency contract and token name, and total amount
* Account name of buyer
* Account name of seller
* Account name of arbiter
* Delivery term, in days

A new deal is created. Its ID is composed from first 32 bits of the
transaction ID.

The other party needs to `accept` the deal within 3 days. If Alice creates
the deal, Bob needs to accept it, and vice versa.

The buyer needs to transfer the whole amount to `escrowescrow` with deal
ID in memo within 3 days after the deal is accepted. 

If the above actions haven't happened within their terms, the deal is
automatically deleted from the contract.

Bob delivers the pumpkins within the delivery term and sends `claim`
transaction. In case the delivery hasn't happened within the term, the
tokens are returned to Alice, and the deal is deleted from the contract.

Alice needs to confirm the deal closure by calling `goodsrcvd` action
within 3 days. This will trigger a transfer of tokens to Bob.

If there's no confirmation within 3 days, the deal is open for
arbitration. Alice can still call `goodsrcvd` and release the funds. At
the same time, the arbiter can call one of two actions: `arbrefund`
would send money back to Alice, or `arbenforce` would send money to Bob.

Alice or Bob can call `cancel` action under the following conditions:

* any time before the deposit is transferred, either party can cancel the deal;

* Only Bob can cancel the deal after the tokens are deposited;




## Copyright and License

Copyright 2018 cc32d9@gmail.com

This work is licensed under a Creative Commons Attribution 4.0
International License.

http://creativecommons.org/licenses/by/4.0/
