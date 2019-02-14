# Universal EOS escrow contract

This is an escrow service that anyone in EOS network can use. It is
accepting deals in any valid tokens in EOS network, and providing easy
means for integrating it into e-commerce solutions.

The workflow is as follows:

Alice wants to buy pumpkins from Bob and she pays with FARMER token.

They trust Chris to be the arbiter in case of a conflict.

Chris registers himself as an arbiter with `setarbiter` transaction,
providing his email address, name, and optionally description, website
URL and phone number. Optionaly Chris can indicate a 2-letter country
code if he wants to operate in specific country only. If Chris needs to
modify some fields, he can send `setarbiter` again.

Anyone can create a deal in `escrowescrow`, by sending an action
`newdeal` with the following attributes:

* Text description of a deal
* Currency contract and token name, and total amount
* Account name of buyer
* Account name of seller
* Account name of arbiter
* Delivery term, in days

A new deal is created. Its ID is composed from first 32 bits of the
transaction ID. If the deal is created by Alice or Bob, it is considered
automatically accepted by corresponding party. Alice must have a
positive balance of specified tokens on her account.

Both parties need to `accept` the deal within 3 days.

The buyer needs to transfer the whole amount to `escrowescrow` with deal
ID in memo within 3 days after the deal is accepted. 

If the above actions haven't happened within their terms, the deal is
automatically deleted from the contract.

Bob delivers the pumpkins within the delivery term and sends `delivered`
transaction. In case the delivery hasn't happened within the term, the
tokens are returned to Alice, and the deal is deleted from the contract.

Alice needs to confirm the deal closure by calling `goodsrcvd` action
within 3 days. This will trigger a transfer of tokens to Bob. Alice can
also confirm if Bob didn't send the `delivered` action.

If Bob needs more time, Alice can call `extend` action and extend the
delivery term by specified number of days.

If there's no confirmation within 3 days, the deal is open for
arbitration. Alice can still call `goodsrcvd` and release the funds. At
the same time, the arbiter can call one of two actions: `arbrefund`
would send money back to Alice, or `arbenforce` would send money to Bob.

Alice or Bob can call `cancel` action under the following conditions:

* any time before the deposit is transferred, either party can cancel the deal;

* Only Bob can cancel the deal after the tokens are deposited;


If Chris is no longer willing to be an arbiter, he sends a `delarbiter`
transaction. If there are any ongoing deals, Chris will be removed from
the list of arbiters as soon as all those deals close.



## Sponsors, Copyright and License

This development is sponsored by EOS Geneva (https://eosgeneva.io/), BP accounut name: `switzerlanda`.

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
