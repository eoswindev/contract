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
#include <boost/algorithm/string.hpp>
#include "random.hpp"
#include "token.hpp"

namespace eoswin {
    using namespace eosio;
    using namespace std;


    #define EOS_SYMBOL S(4, EOS)
    #define GAME_SYMBOL S(4, LUCKY)

    #define BLACK_JACK_ACCOUNT N(iamblackjack)
    #define GAME_TOKEN_ACCOUNT N(eoslucktoken)
    #define TEAM_ACCOUNT N(luckyaddress)
    #define EOS_ACCOUNT  N(eosio.token)
    #define DIVI_ACCOUNT N(eoswinbonus1)

    #define G_ID_ACTIVE                 101
    #define G_ID_GAME_ID                102
    #define G_ID_BANKER_ACTIVE          103
    #define G_ID_BANKER_NUM             104
    #define G_ID_HISTORY_SIZE           105
    #define G_ID_LUCKY_FACTOR           106
    #define G_ID_COMMON_WIN_COUNT       107
    #define G_ID_DOUBLE_WIN_COUNT       108
    #define G_ID_LOSE_COUNT             109
    #define G_ID_PUSH_COUNT             110
    #define G_ID_ACTIVITY_START_TIME    111
    #define G_ID_ACTIVITY_END_TIME      112
    #define G_ID_DIVIDE_INTERVAL        113
    #define G_ID_DIVIDE_RATIO           114


    #define PLAYER_TOKEN_FEE            8
    #define INVITER_TOKEN_FEE           1
    #define TEAM_TOKEN_FEE              1

    #define GAME_MAX_TIME               1200*1e6
    #define GAME_MAX_HISTORY_SIZE       50
    #define GAME_MAX_POINTS             21
    #define CARD_UNKOWN                 255
    #define BANKER_MAX_NUM              100
    #define GAME_STATUS_BEGAN           1
    #define GAME_STATUS_STANDED         2
    #define GAME_STATUS_CLOSED          3

    #define CARDS_STATUS_NONE           0
    #define CARDS_STATUS_HARD           1
    #define CARDS_STATUS_SOFT           2

    #define MAX_BET_FEE                 0.005

    // @abi table globals i64
    struct global {
        uint64_t id;
        uint64_t val;

        uint64_t primary_key() const { return id; }
        EOSLIB_SERIALIZE(global, (id)(val));
    };
    typedef multi_index<N(globals), global> globals_table;

    // @abi table tradetokens i64
    struct tradetoken {
        asset risk_line;
        account_name token_account = 0;
        asset min;
        asset in;
        asset out;
        bool divide_open = false;
        asset divide_balance;
        uint64_t divide_time;
        uint64_t play_times;
        uint64_t primary_key() const { return risk_line.symbol.name(); }
        EOSLIB_SERIALIZE(tradetoken, (risk_line)(token_account)(min)(in)(out)(divide_open)(divide_balance)(divide_time)(play_times));
    };
    typedef multi_index<N(tradetokens), tradetoken> tradetokens_table;

    // @abi table history i64
    struct game_base_item {
        uint64_t game_id;
        uint64_t start_time;
        uint64_t close_time;
        uint8_t  deal_num;

        // banker status
        account_name banker = BLACK_JACK_ACCOUNT;
        vector<uint8_t> banker_cards;

        // player status
        account_name player;
        account_name referal;
        asset bet;
        vector<asset> payout;
        vector<uint8_t> player_cards;
        asset  insure;
        bool    double_down = false;
        uint8_t insure_status = 0;
        bool   surrender = false;
        bool   manual_stand = false;
        uint64_t  status;
        string result;

        uint64_t primary_key()const { return game_id; }

        EOSLIB_SERIALIZE(game_base_item, (game_id)(start_time)(close_time)(deal_num)(banker)(banker_cards)(player)(referal)(bet)(payout)(player_cards)(insure)(double_down)(insure_status)(surrender)(manual_stand)(status)(result));
    };
    typedef multi_index<N(history), game_base_item> history_table;

    // @abi table games i64
    struct game_item : public game_base_item  {
        uint64_t primary_key()const { return player; }
        uint64_t byid()const {return game_id;} 
        uint64_t bybanker()const {return banker;}
        uint64_t bystatus()const {return status;}
        uint64_t bytime()const {return start_time;}

        EOSLIB_SERIALIZE(game_item, (game_id)(start_time)(close_time)(deal_num)(banker)(banker_cards)(player)(referal)(bet)(payout)(player_cards)(insure)(double_down)(insure_status)(surrender)(manual_stand)(status)(result));
    };
    typedef multi_index<N(games), game_item,
    indexed_by< N(byid), const_mem_fun<game_item, uint64_t, &game_item::byid>  >,
    indexed_by< N(bybanker), const_mem_fun<game_item, uint64_t, &game_item::bybanker>  >,
    indexed_by< N(bystatus), const_mem_fun<game_item, uint64_t, &game_item::bystatus>  >,
    indexed_by< N(bytime), const_mem_fun<game_item, uint64_t, &game_item::bytime>  >
    > games_table;

    //@abi table players i64
    struct player_item {
        account_name player;
        uint64_t play_times;
        asset in;
        asset out;
        uint64_t last_start_time;
        uint64_t last_close_time;
        asset activity_in;
        asset activity_out;

        uint64_t primary_key()const { return player; }
        long double byin() const { return -in.amount;}
        long double byactin() const {
            globals_table g(BLACK_JACK_ACCOUNT, BLACK_JACK_ACCOUNT);
            auto s_pos = g.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = g.find(G_ID_ACTIVITY_END_TIME);
            if (s_pos == g.end() || e_pos == g.end()) {
                return -activity_in.amount;
            }

            if (last_start_time < s_pos->val || last_start_time > e_pos->val) {
                return activity_in.amount;
            } else {
                return -activity_in.amount;
            }
        }
        EOSLIB_SERIALIZE(player_item, (player)(play_times)(in)(out)(last_start_time)(last_close_time)(activity_in)(activity_out));
    };
    typedef multi_index<N(players), player_item,
    indexed_by< N(byin), const_mem_fun<player_item, long double, &player_item::byin>  >,
    indexed_by< N(byactin), const_mem_fun<player_item, long double, &player_item::byactin>  >
    > players_table;

    class blackjack: public contract {
        public: 
        const uint8_t A[8][2] = {
            {1,11}, {2,12}, {3,13}, {4,14}, {5,15}, {6,16}, {7,17}, {8,18}
        };

        public:
        blackjack(account_name name);
        ~blackjack();

        // @abi action
        void init();

        void seed();
        uint8_t cal_points(const vector<uint8_t>& cards);
        uint8_t random_card(const game_item& gm, const vector<uint8_t>& exclude = vector<uint8_t>());
        void deal_to_banker(game_item& gm);
        void replace_banker_first_card(game_item& gm);
        bool is_A(uint8_t card);
        bool is_ten(uint8_t card);
        asset asset_from_vec(const vector<asset>& vec, symbol_type sym) const;
        account_name token_account_for_symbol(symbol_type sym) const;
        void assert_not_operation_if_need_insure(const game_item& gm);

        string card_to_string(uint8_t card);
        string cards_to_string(const vector<uint8_t>& cards);

        void reward_game_token(const game_item& gm);
        void to_dividend(symbol_type sym);

        void transfer(account_name from, account_name to, asset quantity, string memo);

        void new_game(account_name player, account_name banker, asset& bet, account_name referal);

        void double_down(account_name player, const asset& in);

        void insure(account_name player, const asset& in);

        // @abi action
        void setglobal(uint64_t id, uint64_t val);

        // @abi action
        void settoken(asset risk, account_name token_account, asset min, bool divide_open,  asset divide, uint32_t divide_time=now());

        // @abi action
        void dealing(string action, uint64_t game_id);

        // @abi action
        void dealed(string action, uint64_t game_id);

        // @abi action
        void stand(account_name player);

        // @abi action
        void hit(account_name player);

        // @abi action
        void uninsure(account_name player);

        // @abi action
        void surrender(account_name player);

        // @abi action
        void close(uint64_t game_id);

        // @abi action
        void softclose(uint64_t game_id, string reason);

        // @abi action
        void hardclose(uint64_t game_id, string reason);

        // @abi action
        void cleargames(uint32_t num);

        // @abi action
        void receipt(game_item gm, string banker_cards, string player_cards, string memo);

        private:
        random _random;

        globals_table _globals;
        tradetokens_table _tradetokens;

        games_table  _games;
        players_table _players;
        history_table _history;
    };
}


