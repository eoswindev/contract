/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>

#define DICE_ACCOUNT N(eosluckydice)

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class token : public contract {
      public:
         token( account_name self ):contract(self){}

         void create( account_name issuer,
                      asset        maximum_supply);

         void issue( account_name to, asset quantity, string memo );

         void transfer( account_name from,
                        account_name to,
                        asset        quantity,
                        string       memo );
         
         void reclaim(account_name owner, asset quantity, string memo);

         void updateage( account_name act, asset quantity, uint64_t time );
      
         inline asset get_supply( symbol_name sym )const;

         inline asset get_max_supply( symbol_name sym )const;
         
         inline asset get_balance( account_name owner, symbol_name sym )const;

         inline uint64_t get_balance_amount( account_name owner, symbol_name sym )const;

         inline uint64_t get_age(account_name owner, symbol_name sym)const;

      private:
         const uint64_t useconds_claim_token_bonus_interval = 24*3600*uint64_t(1000000);

         ///@abi table accounts i64
         struct account {
            asset    balance;

            uint64_t primary_key()const { return balance.symbol.name(); }
         };

         ///@abi table stat i64
         struct currency_stat {
            asset          supply;
            asset          max_supply;
            account_name   issuer;

            uint64_t primary_key()const { return supply.symbol.name(); }
         };

         ///@abi table details i64
         struct detail {
            symbol_name sym;
            uint64_t age;

            uint64_t primary_key()const { return sym; }
         };

         typedef eosio::multi_index<N(accounts), account> accounts;
         typedef eosio::multi_index<N(stat), currency_stat> stats;
         typedef eosio::multi_index<N(details), detail> details;

         void sub_balance( account_name owner, asset value );
         void add_balance( account_name owner, asset value, account_name ram_payer );

      public:
         struct transfer_args {
            account_name  from;
            account_name  to;
            asset         quantity;
            string        memo;
         };
   };

   asset token::get_supply( symbol_name sym )const
   {
      stats statstable( _self, sym );
      const auto& st = statstable.get( sym );
      return st.supply;
   }

   asset token::get_max_supply( symbol_name sym )const
   {
      stats statstable( _self, sym );
      const auto& st = statstable.get( sym );
      return st.max_supply;
   }

   asset token::get_balance( account_name owner, symbol_name sym )const
   {
      accounts accountstable( _self, owner );
      const auto& ac = accountstable.get( sym );
      return ac.balance;
   }

   uint64_t token::get_balance_amount( account_name owner, symbol_name sym )const
   {
      accounts accountstable( _self, owner );
      const auto& ac = accountstable.find( sym );
      if (ac == accountstable.cend()) {
            return 0;
      }

      return (ac->balance).amount;
   }

   uint64_t token::get_age(account_name owner, symbol_name sym)const
   {
      details ds(_self, owner);
      const auto pos = ds.find( sym );
      if (pos != ds.end()) {
            return pos->age;
      } else {
            return 0;
      }
   }

} /// namespace eosio
