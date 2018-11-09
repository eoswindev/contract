/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/currency.hpp>
#include <eosiolib/transaction.h>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>
#include "config.hpp"
#include "token.hpp"

namespace eoswin {
    using namespace eosio;

    #define ACTIVITY_ACCOUNT N(activity)
    #define TOKEN_ACCOUNT GAME_TOKEN_CONTRACT
    #define HISTORY_NUM 5
    #define CLAIM_BONUS_HISTORY_NUM 100

    #define STATUS_ID_BONUS_ACTIVE 101
    #define STATUS_ID_CLAIM_BONUS_COUNT  102
    #define STATUS_ID_CLAIM_SUB_TOKEN_ACTIVE 103
    #define STATUS_ID_CLAIM_PEROID_BASE_TIME 104
    #define STATUS_ID_CLAIM_PEROID_ACC_INTERVAL 105
    #define STATUS_ID_CLAIM_PEROID_CLAIM_INTERVAL 106
    #define STATUS_ID_TOKEN_SNAPSHOT_TIME         107

	const uint64_t useconds_draw_interval = 2*3600*uint64_t(1000000);
    const uint64_t useconds_claim_token_bonus_interval = 24*3600*uint64_t(1000000);
    const uint16_t max_player_bills_per_round = 50;

    //@abi table global i64
    struct global_item {
        asset    key_price;
        uint64_t cur_round;
        account_name token_account;
        account_name team_account;
        uint8_t  token_bonus;
        uint8_t  team_fee;
        uint8_t  drawer_fee;
        asset    drawer_fee_max;
        uint8_t  mine_probability;
        uint8_t  mine_team_fee;
        uint8_t  mine_referral_fee;
        asset    total_claimed_token_bonus;
        uint32_t total_users;
        bool  active;

        EOSLIB_SERIALIZE( global_item, (key_price)(cur_round)(token_account)(team_account)(token_bonus)(team_fee)(drawer_fee)(drawer_fee_max)(mine_probability)(mine_team_fee)(mine_referral_fee)(total_claimed_token_bonus)(total_users)(active) )
    };
    typedef eosio::singleton<N(global), global_item> global_singleton;

    //@abi table rounds i64
    struct round_item {
        uint64_t round;
        uint64_t bills_num;
        uint64_t last_key;
        asset    reward_bucket;
        account_name draw_account;
        uint64_t start_time;
        uint64_t draw_time;
        uint64_t lucky_key;
        account_name lucky_player;
        uint64_t primary_key()const {return round;}

        EOSLIB_SERIALIZE( round_item, (round)(bills_num)(last_key)(reward_bucket)(draw_account)(start_time)(draw_time)(lucky_key)(lucky_player) )
    };
    typedef eosio::multi_index<N(rounds), round_item> rounds_table;

    //@abi table roundfee i64
    struct roundfee_item {
        uint64_t round;
        asset    to_team;
        asset    to_bonus;
        asset    to_drawer;
        asset    to_lucky;
        uint64_t primary_key()const {return round;}
        EOSLIB_SERIALIZE( roundfee_item, (round)(to_team)(to_bonus)(to_drawer)(to_lucky) )
    };
    typedef eosio::multi_index<N(roundfee), roundfee_item> roundfee_table;

    //@abi table bills i64
    struct bill_item {
        uint64_t id;
        account_name player;
        uint64_t low;
        uint64_t high;
        asset mine_token;
        account_name referal;
        uint64_t time;

        uint64_t primary_key()const {return id;}
        account_name byplayer()const { return player; }

        EOSLIB_SERIALIZE( bill_item, (id)(player)(low)(high)(mine_token)(referal)(time))
    };
    typedef eosio::multi_index<N(bills), bill_item,
    indexed_by<N(byplayer), const_mem_fun<bill_item, account_name, &bill_item::byplayer>  >
    > bills_table;

    //@abi table players i64
    struct player_item {
        account_name player;
        uint16_t bills_num;

        uint64_t primary_key()const {return player;}

        EOSLIB_SERIALIZE( player_item, (player)(bills_num))
    };
    typedef eosio::multi_index<N(players), player_item> players_table;

    //@abi table status i64
    struct status {
        uint64_t id;
        uint64_t val;

        uint64_t primary_key()const {return id;}
        EOSLIB_SERIALIZE( status, (id)(val))
    };
    typedef eosio::multi_index<N(status), status> status_table;

    //@abi table claims i64
    struct claim_item {
        uint64_t id;
        account_name player;
        asset bonus;
        uint64_t time;

        uint64_t primary_key()const {return id;}
        EOSLIB_SERIALIZE(claim_item, (id)(player)(bonus)(time))
    };
    typedef eosio::multi_index<N(claims), claim_item> claims_table;

    //@abi table dividends i64
    struct dividend_item {
        uint64_t id;
        account_name player;
        vector<asset> bonus;
        uint64_t time;

        uint64_t primary_key()const {return id;}
        EOSLIB_SERIALIZE(dividend_item, (id)(player)(bonus)(time))
    };
    typedef eosio::multi_index<N(dividends), dividend_item> dividends_table;

    //@abi table tokens i64
    struct token_item {
        asset balance;

        uint64_t primary_key()const {return balance.symbol.name();}
        EOSLIB_SERIALIZE(token_item, (balance))
    };
    typedef eosio::multi_index<N(tokens), token_item> tokens_table;

	class lottery : public eosio::contract {
		public:
			lottery(account_name name);

	        ~lottery();

            global_item get_default_parameters();

            void issue_game_token(account_name to, asset quantity, string memo);

            void get_stage_fees(const asset& bucket, uint8_t& lucky_fee, uint8_t& team_fee);

            /// @abi action
            void active(account_name actor);

            ///@abi action
            void setactived(bool actived);

            // @abi action
            void setstatus(uint64_t id, uint64_t val);

            void newround(account_name actor);

            void endround();

			void transfer(account_name from, account_name to);

            bill_item buykeys(account_name buyer, asset quantity, string memo);

            uint64_t mine(uint64_t keynum);

            uint64_t randomkey(uint64_t max);

            /// @abi action
            void delaydraw(account_name drawer);

            /// @abi action
            void drawing(account_name drawer);

            /// @abi action
			void draw(account_name drawer);

            /// @abi action
            void claimbonus(account_name actor, account_name owner);

            asset divide_token(account_name to, account_name token_account, symbol_name sym, const double percent);

            void snapshot_token_balance();

            /// @abi action
            void activesub(bool active);

            /// @abi action
            void activebonus(bool active);

            /// @abi action
            void eraseclaims();

            void erasehistory(uint64_t offset = 1, uint64_t limit = 1);

		private:
            global_singleton _global;
            global_item      _global_state;
            rounds_table     _rounds;
            roundfee_table   _roundfee;

            token            _game_token;
            symbol_name      _game_token_symbol;

            status_table _status;
            dividends_table _dividends;
            tokens_table    _tokens;
	};
}