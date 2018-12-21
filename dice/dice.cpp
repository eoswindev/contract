#include <vector>
#include <string>
#include <eosiolib/eosio.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.h>
#include <eosiolib/currency.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/time.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/action.hpp>
#include <boost/algorithm/string.hpp>
#include <eosio.token.hpp>
#include "config.hpp"

#define GLOBAL_ID_BET 101
#define GLOBAL_ID_HISTORY_INDEX 104
#define GLOBAL_ID_LARGE_HISTORY_INDEX 105
#define GLOBAL_ID_HIGH_ODDS_HISTORY_INDEX 106
#define GLOBAL_ID_ACTIVE                109        

#define GLOBAL_ID_ACCU_START_TIME 102 
#define GLOBAL_ID_ACCU_STOP_TIME 103 
#define GLOBAL_ID_STATIC_TOTAL 107   
#define GLOBAL_ID_STATIC_DAILY 108   
#define GLOBAL_ID_LUCKNUM_START_TIME  110       
#define GLOBAL_ID_LUCKNUM_STOP_TIME  111        
#define GLOBAL_ID_LUCKNUM_START_BETNUMS  112        
#define GLOBAL_ID_LUCKNUM_STOP_BETNUMS  113        
#define GLOBAL_ID_LUCK_DRAW_TOTAL       114        
#define GLOBAL_ID_LUCK_DRAW_ACTIVE      115        
#define GLOBAL_ID_LUCK_DRAW_LUCKY       116        
#define GLOBAL_ID_TOKEN_MULTIPLE        118       
#define GLOBAL_ID_MIN_JACKPOT_BET       119
#define GLOBAL_ID_JACKPOT_ACTIVE        120
#define GLOBAL_ID_DIVIDEND_PERCENT      121
#define GLOBAL_ID_MAX_BET_PER           122

#define BET_HISTORY_LEN 40
#define WONDER_HIGH_ODDS 20
#define FEE 2 
#define SINGLE_BET_MAX_PERCENT 1.5
#define BETTOR_TOKEN_FEE 8      
#define INVITER_TOKEN_FEE 1
#define ROLL_BORDER_MIN 2
#define ROLL_BORDER_MAX 97
#define ROLL_TYPE_SMALL 1 
#define ROLL_TYPE_BIG 2 
#define BET_MAX_NUM 100 
#define INVITE_BONUS 0.005 
#define JACKPOT_BONUS 0.005 
#define TOP_NOTICE 1
#define DAY_SECONDS 86400
#define HOUR_SECONDS 3600
#define LUCK_DRAW_MAX 10001

using namespace std;

/********* random ***********/
namespace eoswin {
  class random {
    public:
      template<class T>
      struct data {
        T content;
        int block;
        int prefix;
        uint64_t time;

        data(T t) {
          content   = t;
          block  = tapos_block_num();
          prefix = tapos_block_prefix();
          time   = current_time();
        }
      };

      struct st_seeds {
        checksum256 seed1;
        checksum256 seed2;
      };

    public:
      random();
      ~random();

      template<class T>
      checksum256 create_sys_seed(T mixed) const;

      void seed(checksum256 sseed, checksum256 useed);

      void mixseed(checksum256& sseed, checksum256& useed, checksum256& result) const;

      // generator number ranged [0, max-1]
      uint64_t generator(uint64_t max = 101);

      uint64_t gen(checksum256& seed, uint64_t max = 101) const;

      checksum256 get_sys_seed() const;
      checksum256 get_user_seed() const;
      checksum256 get_mixed() const;
      checksum256 get_seed() const;
    private:
      checksum256 _sseed;
      checksum256 _useed;
      checksum256 _mixed;
      checksum256 _seed;
  };

  random::random() {}
  random::~random() {}

  template<class T>
  checksum256 random::create_sys_seed(T mixed) const {
    checksum256 result;
    data<T> mixed_block(mixed);
    const char *mixed_char = reinterpret_cast<const char *>(&mixed_block);
    sha256((char *)mixed_char, sizeof(mixed_block), &result);
    return result;
  }

  void random::seed(checksum256 sseed, checksum256 useed) {
    _sseed = sseed;
    _useed = useed;
    mixseed(_sseed, _useed, _mixed);
    _seed  = _mixed;
  }

  void random::mixseed(checksum256& sseed, checksum256& useed, checksum256& result) const {
    st_seeds seeds;
    seeds.seed1 = sseed;
    seeds.seed2 = useed;
    sha256( (char *)&seeds.seed1, sizeof(seeds.seed1) * 2, &result);
  }

  uint64_t random::generator(uint64_t max) {
    mixseed(_mixed, _seed, _seed);

    uint64_t r = gen(_seed, max);
    
    return r;
  }

  uint64_t random::gen(checksum256& seed, uint64_t max) const {
    if (max <= 0) {
        return 0;
    }
    const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&seed);
    uint64_t r = p64[1] % max;
    return r;
  }

  checksum256 random::get_sys_seed() const {
    return _sseed;   
  }

  checksum256 random::get_user_seed() const {
    return _useed;
  }

  checksum256 random::get_mixed() const {
    return _mixed;
  }

  checksum256 random::get_seed() const {
    return _seed;
  }
}
/********************************* bet *********************************/

class dice : public eosio::contract {
  public:
    const uint64_t to_bonus_bucket_interval = 1*3600*uint64_t(1000000);
    const uint64_t luck_draw_interval = 1*3600*uint64_t(1000000);

    eosio::token _eosio_token;
    eosio::token _game_token;
    using contract::contract;

    account_name _code;
    void setCode(account_name code) {
      _code = code;
    }

    //@abi table activebets
    //@abi table highbets
    //@abi table largebets
    struct bet
    {
      uint64_t id;
      uint64_t bet_id;
      account_name contract;
      account_name bettor;
      account_name inviter;
      uint64_t bet_amt;
      vector<eosio::asset> payout;
      uint8_t roll_type;
      uint64_t roll_border;
      uint64_t roll_value;
      checksum256 seed;
      eosio::time_point_sec time;
      uint64_t primary_key() const { return id; };
      EOSLIB_SERIALIZE(bet, (id)(bet_id)(contract)(bettor)(inviter)(bet_amt)(payout)(roll_type)(roll_border)(roll_value)(seed)(time));
    };
    typedef eosio::multi_index<N(activebets), bet> bet_index;
    bet_index _bets;

    typedef eosio::multi_index<N(highbets), bet> high_odds_index;
    high_odds_index _high_odds_bets;

    typedef eosio::multi_index<N(largebets), bet> large_eos_index;
    large_eos_index _large_eos_bets;
    
    //@abi table globalvars
    struct globalvar
    {
      uint64_t id;
      uint64_t val;
      uint64_t primary_key() const { return id; };
      EOSLIB_SERIALIZE(globalvar, (id)(val));
    };
    typedef eosio::multi_index<N(globalvars), globalvar> global_index;
    global_index _globals;

    //@abi table tradetokens
    struct tradetoken
    {
      eosio::symbol_name name;
      account_name contract;
      uint64_t in;  
      uint64_t out; 
      uint64_t protect; 
      uint64_t times; 
      uint64_t divi_time; 
      uint64_t divi_balance; 
      uint64_t min_bet; 
      uint64_t large_bet; 
      uint64_t primary_key() const { return name; };
      EOSLIB_SERIALIZE(tradetoken, (name)(contract)(in)(out)(protect)(times)(divi_time)(divi_balance)(min_bet)(large_bet));
    };
    typedef eosio::multi_index<N(tradetokens), tradetoken> _tradetoken_index;
    _tradetoken_index _trades;

    //@abi table notices
    struct notice
    {
      uint64_t id;
      string val;
      uint64_t primary_key() const { return id; };
      EOSLIB_SERIALIZE(notice, (id)(val));
    };
    typedef eosio::multi_index<N(notices), notice> _notice_index;
    _notice_index _notices;

    struct assetitem
    {
      string symbol;
      uint64_t accu_in;
      uint64_t accu_out;
    };

    //@abi table players
    //@abi table ranks
    //@abi table dailys
    struct player
    {
      account_name account;
      eosio::time_point_sec last_bettime;
      eosio::asset last_betin;
      eosio::asset last_payout;
      vector<assetitem> asset_items;
      uint64_t primary_key() const { return account; };
      double byeosin() const {
        for(auto it = asset_items.begin(); it != asset_items.end(); ++it) {
          if (it->symbol == "EOS") {
            return -it->accu_in;
          }
        }
        return 0;
      }
      EOSLIB_SERIALIZE(player, (account)(last_bettime)(last_betin)(last_payout)(asset_items));
    };

    typedef eosio::multi_index<N(players), player> _player_index;
    _player_index _playerlists;

    typedef eosio::multi_index<N(ranks), player> _rank_index;
    _rank_index _ranklists;

    typedef eosio::multi_index<N(dailys), player> _daily_index;
    _daily_index _dailylists;

    ///@abi table luckers i64
    struct lucker {
      account_name account;
      uint64_t last_bet_time_sec;  
      uint64_t draw_time;
      uint64_t roll_value;

      uint64_t primary_key() const {return account;}
      EOSLIB_SERIALIZE(lucker, (account)(last_bet_time_sec)(draw_time)(roll_value))
    };
    typedef eosio::multi_index<N(luckers), lucker> luckers_table;
    luckers_table _luckers;

    //@abi table luckys
    struct luckyreward
    {
      uint64_t id;
      uint64_t number;
      eosio::asset reward;
      uint64_t primary_key() const { return id; };
      EOSLIB_SERIALIZE(luckyreward, (id)(number)(reward));
    };
    typedef eosio::multi_index<N(luckys), luckyreward> _luckyreward_index;
    _luckyreward_index _luckyrewards;

    eoswin::random _random;
    checksum256 _seed;
    
    dice(account_name self) : contract(self),
                                 _bets(self, self),
                                 _globals(self, self),
                                 _high_odds_bets(self, self),
                                 _large_eos_bets(self, self),
                                 _eosio_token(TOKEN_CONTRACT),
                                 _game_token(GAME_TOKEN_CONTRACT),
                                 _trades(self, _self),
                                 _notices(self, _self),
                                 _playerlists(self, _self),
                                 _ranklists(self, _self),
                                 _dailylists(self, _self),
                                 _luckers(self, self),
                                 _luckyrewards(self, _self)

    {
      
    }

    /// @abi action
    void setactive(bool active) {
      require_auth(_self);

      auto pos = _globals.find(GLOBAL_ID_ACTIVE);
      if (pos == _globals.end()) {
        _globals.emplace(_self, [&](auto& a) {
          a.id = GLOBAL_ID_ACTIVE;
          a.val = active;
        });
      } else {
        _globals.modify(pos, 0, [&](auto& a) {
          a.val = active;
        });
      }
    }

    /// @abi action
    void init()
    {
      require_auth(_self);

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_BET;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_HISTORY_INDEX;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_LARGE_HISTORY_INDEX;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_HIGH_ODDS_HISTORY_INDEX;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_STATIC_TOTAL;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_STATIC_DAILY;
        a.val = 0;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_MIN_JACKPOT_BET;
        a.val = 2500;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_JACKPOT_ACTIVE;
        a.val = 1;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_DIVIDEND_PERCENT;
        a.val = 25;
      });

      _globals.emplace(_self, [&](auto &a) {
        a.id = GLOBAL_ID_MAX_BET_PER;
        a.val = 100;
      });
      
      _notices.emplace(_self, [&](auto &a) {
        a.id = TOP_NOTICE;
        a.val = "";
      });

      init_all_trade();
    }

    void init_all_trade() {
      init_trade_token(EOS_SYMBOL, TOKEN_CONTRACT);
      init_trade_token(GAME_SYMBOL, GAME_TOKEN_CONTRACT);
      init_trade_token(ADD_SYMBOL, ADD_CONTRACT);
      init_trade_token(ATD_SYMBOL, ATD_CONTRACT);
      init_trade_token(DAC_SYMBOL, DAC_CONTRACT);
      init_trade_token(HORUS_SYMBOL, HORUS_CONTRACT);
      init_trade_token(IQ_SYMBOL, IQ_CONTRACT);
      init_trade_token(KARMA_SYMBOL, KARMA_CONTRACT);
      init_trade_token(TP_SYMBOL, TP_CONTRACT);
      init_trade_token(MEET_SYMBOL, MEET_CONTRACT);
      init_trade_token(BLACK_SYMBOL, BLACK_CONTRACT);
    }

    void init_trade_token(eosio::symbol_name sym, account_name contract)
    {
      _trades.emplace(_self, [&](auto &a) {
        a.name = eosio::symbol_type(sym).name();
        a.contract = contract;
        a.in = 0;
        a.out = 0;
        a.protect = 0;
        a.times = 0;
        a.divi_time = current_time();
        a.divi_balance = 0;
      });
    }

    /// @abi action
    void setnotice(string notice)
    {
      require_auth(_self);
      auto iter = _notices.find(TOP_NOTICE);
      if (iter != _notices.end()) {
        _notices.modify(iter, 0, [&](auto &a) {
          a.val = notice;
        });
      }
    }

    /// @abi action
    void setluckrwd(uint64_t id, uint64_t number, eosio::asset reward)
    {
      require_auth(_self);
      auto iter = _luckyrewards.find(id);
      if (iter == _luckyrewards.end()) {
        _luckyrewards.emplace(_self, [&](auto &a) {
          a.id = id;
          a.number = number;
          a.reward = reward;
        });
      } else {
        _luckyrewards.modify(iter, 0, [&](auto &a) {
          a.number = number;
          a.reward = reward;
        });
      }
    }

    /// @abi action
    void setglobal(uint64_t id, uint64_t value)
    {
      require_auth(_self);
      auto iter = _globals.find(id);
      if (iter == _globals.end()) {
        _globals.emplace(_self, [&](auto &a) {
          a.id = id;
          a.val = value;
        });
      } else {
        _globals.modify(iter, 0, [&](auto &a) {
          a.val = value;
        });
      }
    }

    void to_bonus_bucket(eosio::symbol_type sym) 
    {
      eosio::symbol_name sym_name = eosio::symbol_type(sym).name();
      auto trade_iter = _trades.find(sym_name);
      
      auto divi_time = trade_iter->divi_time;
      auto ct = current_time();

      if (ct < (divi_time + to_bonus_bucket_interval)) {
        return;
      }
      
      uint64_t last_divi_balance = trade_iter->divi_balance;
      eosio::token bet_token(trade_iter->contract);
      auto balance = bet_token.get_balance(_self, sym_name);
      uint64_t dividends = 0;

      if (last_divi_balance < balance.amount) {
        auto iter = _globals.find(GLOBAL_ID_DIVIDEND_PERCENT);
        
        uint64_t dividend_pecent = iter->val;
        if (dividend_pecent >= 75) {
          dividend_pecent = 75;
        }
        dividends = (balance.amount - last_divi_balance) * dividend_pecent / 100;
      }

      if (dividends <= 10000) {
        return;
      }

      _trades.modify(trade_iter, 0, [&](auto &a) {
        a.divi_balance = balance.amount - dividends;
        a.divi_time = ct;
      });
      INLINE_ACTION_SENDER(eosio::token, transfer)(trade_iter->contract, {_self,N(active)}, {_self, DIVI_ACCOUNT, eosio::asset(dividends, sym), "To EOS.Win Bonus Pool [https://eos.win/dice]"} );
    }

    void reward_game_token(account_name bettor, account_name inviter, eosio::asset quantity) {
      auto pos = _globals.find(GLOBAL_ID_TOKEN_MULTIPLE);
      uint64_t multiple = 1;
      if (pos != _globals.end()) {
        multiple = pos->val;
      }

      auto num = quantity.amount / 2 * multiple;
      num = quantity.amount % 2 == 0 ? num : num + 1;
      if (num <= 0) {
        return;
      }
      auto reward = eosio::asset(num, GAME_SYMBOL);

      auto balance = _game_token.get_balance(GAME_TOKEN_CONTRACT, eosio::symbol_type(GAME_SYMBOL).name());
      reward = reward > balance ? balance : reward;

      auto to_better = reward * BETTOR_TOKEN_FEE / 10;
      auto to_team   = reward - to_better;
      if (inviter != TEAM_ACCOUNT) {
        auto to_inviter = reward * INVITER_TOKEN_FEE / 10;
        if (to_inviter.amount > 0) {
          to_team.set_amount(to_team.amount - to_inviter.amount);

          if (is_account(inviter)) {
            INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_CONTRACT, {GAME_TOKEN_CONTRACT,N(active)}, {GAME_TOKEN_CONTRACT, inviter, to_inviter, "LUCKY token for inviter [https://eos.win]"} );
          }
        }
      }

      if (to_better.amount > 0) {
        INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_CONTRACT, {GAME_TOKEN_CONTRACT,N(active)}, {GAME_TOKEN_CONTRACT, bettor, to_better, "LUCKY token for player [https://eos.win]"} );
      }

      if (to_team.amount > 0) {
        INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_CONTRACT, {GAME_TOKEN_CONTRACT,N(active)}, {GAME_TOKEN_CONTRACT, TEAM_ACCOUNT, to_team, "LUCKY token for team [https://eos.win]"} );
      }
    }
    
    void transfer(account_name from, account_name to, eosio::asset quantity, string memo)
    {
      eosio::currency::transfer t = {from, to, quantity, memo};
      if (t.from == _self || t.to != _self)
      {
        return;
      }

      if (t.from == N(eosio.stake) || t.from == N(tptvotepools)) 
      {
        return;
      }


      auto tx_size = transaction_size();
      char tx[tx_size];
      auto read_size = read_transaction(tx, tx_size);
      eosio_assert( tx_size == read_size, "read_transaction failed");
      auto trx = eosio::unpack<eosio::transaction>( tx, read_size );
      eosio::action first_action = trx.actions.front();
      eosio_assert(first_action.name == N(transfer) && first_action.account == _code, "wrong transaction");

      check_symbol_code(t.quantity);
      eosio_assert(t.quantity.is_valid(), "Invalid transfer amount.");
      
      int64_t amt = t.quantity.amount;
      eosio_assert(amt > 0, "Transfer amount not positive");
      
      eosio::symbol_name sym_name = eosio::symbol_type(t.quantity.symbol).name();
      auto trade_iter = _trades.find(sym_name);
      eosio::token bet_token(trade_iter->contract);
      eosio::asset balance = bet_token.get_balance(_self, sym_name);
 
      if (t.memo == "deposit") {
        if (trade_iter != _trades.end()) {
            _trades.modify(trade_iter, 0, [&](auto& info) {
                info.divi_balance += t.quantity.amount;
            });
        }
      } else {
        auto active_pos = _globals.find(GLOBAL_ID_ACTIVE);
        eosio_assert(active_pos != _globals.end() && active_pos->val, "Maintaining ...");

        int64_t max = (balance.amount * SINGLE_BET_MAX_PERCENT / 100);
        eosio_assert(amt <= max, "Bet amount exceeds max amount.");

        auto trade_iter = _trades.find(sym_name);
        eosio_assert(balance.amount >= trade_iter->protect, "Game under maintain, stay tuned.");
        
        eosio_assert(t.memo.empty() == false, "Memo is for dice info, cannot be empty.");

        vector<string> pieces;
        boost::split(pieces, t.memo, boost::is_any_of(","));

        eosio_assert(pieces[0].empty() == false, "Roll type cannot be empty!");
        eosio_assert(pieces[1].empty() == false, "Roll prediction cannot be empty!");

        uint8_t roll_type = atoi( pieces[0].c_str() );
        uint64_t roll_border = atoi( pieces[1].c_str() );

        account_name inviter;
        if (pieces[2].empty() == true) {
          inviter = TEAM_ACCOUNT;
        } else {
          inviter = eosio::string_to_name(pieces[2].c_str());
        }
        eosio_assert(t.from != inviter, "Inviter can't be self");

        auto max_bet_percent_pos = _globals.find(GLOBAL_ID_MAX_BET_PER);
        uint64_t max_bet_percent = max_bet_percent_pos->val;

        int64_t max_reward = get_bet_reward(roll_type, roll_border, amt);
        float max_bet_token = (amt * (balance.amount / max_bet_percent) / max_reward) / 10000.0;
        float min_bet_token = (trade_iter->min_bet) / 10000.0;
        char str[128];
        sprintf(str, "Bet amount must between %f and %f", min_bet_token, max_bet_token);
        eosio_assert(amt >= trade_iter->min_bet && max_reward <= (balance.amount / max_bet_percent), str);

        eosio_assert(roll_border >= ROLL_BORDER_MIN && roll_border <= ROLL_BORDER_MAX, "Bet border must between 2 to 97");

        eosio::transaction r_out;
        auto t_data = make_tuple(t.from, t.quantity, roll_type, roll_border, inviter);
        r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(start), t_data);
        r_out.delay_sec = 1;
        r_out.send(t.from, _self);
      }
    }

    int64_t get_bet_reward(uint8_t roll_type, uint64_t roll_border, int64_t amount)
    {
      uint8_t fit_num;
      if (roll_type == ROLL_TYPE_SMALL)
      {
        fit_num = roll_border;
      }
      else if (roll_type == ROLL_TYPE_BIG)
      {
        fit_num = BET_MAX_NUM - 1 - roll_border;
      }

      int64_t reward_amt = amount * (100 - FEE) / fit_num;

      return reward_amt;
    }

    uint64_t get_random(uint64_t max)
    {
      // auto g = _globals.get(GLOBAL_ID_BET, "global is missing");
      auto sseed = _random.create_sys_seed(0);

      auto s = read_transaction(nullptr, 0);
      char *tx = (char *)malloc(s);
      read_transaction(tx, s);
      checksum256 txid;
      sha256(tx, s, &txid);
      printhex(&txid, sizeof(txid));

      _random.seed(sseed, txid);
      uint64_t roll_value = _random.generator(max);
      _seed = _random.get_seed();

      return roll_value;
    }
    
    uint64_t get_bet_nums()
    {
      uint64_t total = 0;
      for(auto iter = _trades.begin(); iter != _trades.end(); ) 
      {
        total += iter->times;
        iter++;
      }
      return total;
    }


    bool is_lucknum_open()
    {
      auto start_itr = _globals.find(GLOBAL_ID_LUCKNUM_START_TIME);
      uint64_t start_time = 0;
      if (start_itr != _globals.end()) {
        start_time = start_itr->val;
      }

      auto stop_itr = _globals.find(GLOBAL_ID_LUCKNUM_STOP_TIME);
      uint64_t stop_time = 0;
      if (stop_itr != _globals.end()) {
        stop_time = stop_itr->val;
      }

      eosio::time_point_sec cur_time = eosio::time_point_sec( now() );

      if (eosio::time_point_sec(start_time) <= cur_time &&  cur_time <= eosio::time_point_sec(stop_time)) {
        return true;
      }

      uint64_t total_bet_nums = get_bet_nums();
      auto start_betnums_itr = _globals.find(GLOBAL_ID_LUCKNUM_START_BETNUMS);
      uint64_t start_betnums = 0;
      if (start_betnums_itr != _globals.end()) {
        start_betnums = start_betnums_itr->val;
      }

      auto stop_betnums_itr = _globals.find(GLOBAL_ID_LUCKNUM_STOP_BETNUMS);
      uint64_t stop_betnums = 0;
      if (stop_betnums_itr != _globals.end()) {
        stop_betnums = stop_betnums_itr->val;
      }
      
      if (start_betnums <= total_bet_nums && total_bet_nums <= stop_betnums) {
        return true;
      }
      return false;
    }

    /// @abi action
    void start(account_name bettor, eosio::asset bet_asset, 
                   uint8_t roll_type, uint64_t roll_border, account_name inviter)
    {
      require_auth(_self);
      eosio::transaction r_out;
      auto t_data = make_tuple(bettor, bet_asset, roll_type, roll_border, inviter);
      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(resolved), t_data);
      r_out.delay_sec = 0;
      r_out.send(bettor, _self);
    }

    /// @abi action
    void resolved(account_name bettor, eosio::asset bet_asset, 
                   uint8_t roll_type, uint64_t roll_border, account_name inviter)
    {
      require_auth(_self);
      eosio::symbol_name sym_name = eosio::symbol_type(bet_asset.symbol).name();
      auto trade_iter = _trades.find(sym_name);

      uint64_t roll_value = get_random(BET_MAX_NUM);
      auto global_itr = _globals.find(GLOBAL_ID_BET);
      eosio_assert(global_itr != _globals.end(), "Unknown global id");
      uint64_t last_bet_id = global_itr->val;
      uint64_t cur_bet_id = last_bet_id + 1;

      _globals.modify(global_itr, 0, [&](auto& a) {
        a.val = cur_bet_id;
      });

      vector<eosio::asset> payout_list;
      eosio::asset payout;

      uint32_t _now = now();

      if ( (roll_type == ROLL_TYPE_SMALL && roll_value < roll_border) || (roll_type == ROLL_TYPE_BIG && roll_value > roll_border) )
      {
        int64_t reward_amt = get_bet_reward(roll_type, roll_border, bet_asset.amount);
        payout = eosio::asset(reward_amt, bet_asset.symbol);

        char str[128];
        sprintf(str, "Bet id: %lld. You win! Remember to claim your dividens with your LUCKY token! https://eos.win", cur_bet_id);
        INLINE_ACTION_SENDER(eosio::token, transfer)(trade_iter->contract, {_self, N(active)}, {_self, bettor, payout, string(str)} );
        
        _trades.modify(trade_iter, 0, [&](auto& a) {
          a.out += reward_amt;
        });
      }
      else
      {
        payout = eosio::asset(0, bet_asset.symbol);
      }
      
      payout_list.push_back(payout);
      
      to_jackpot(bettor, bet_asset);
      if (bet_asset.symbol == EOS_SYMBOL && is_lucknum_open())
      {
        for(auto lucky_iter = _luckyrewards.begin(); lucky_iter != _luckyrewards.end(); ) 
        {
          if ( (roll_value == lucky_iter->number) && lucky_iter->reward.amount > 0) 
          {
            eosio::symbol_name lucky_sym_name = eosio::symbol_type(lucky_iter->reward.symbol).name();
            auto lucky_trade_iter = _trades.find(lucky_sym_name);
            
            char str[128];
            sprintf(str, "Hit magic number! You got extra bonus! https://eos.win");
            eosio::print(string(str).c_str());
            INLINE_ACTION_SENDER(eosio::token, transfer)(lucky_trade_iter->contract, {_self, N(active)}, {_self, bettor, lucky_iter->reward, string(str)} );
            
            payout_list.push_back(lucky_iter->reward);
          }
          lucky_iter++;
        }
      }

      if (bet_asset.symbol == EOS_SYMBOL) {
        auto lucker_pos = _luckers.find(bettor);
        if (lucker_pos == _luckers.end()) {
          lucker_pos = _luckers.emplace(_self, [&](auto& info) {
            info.account = bettor;
            info.last_bet_time_sec = _now;
            info.draw_time = 0;
            info.roll_value   = 0;
          });
        } else {
          _luckers.modify(lucker_pos, 0, [&](auto& info) {
            info.last_bet_time_sec = _now;
          });
        }
      }

      eosio::time_point_sec time = eosio::time_point_sec( _now );
      
      save_rank_list(bettor, bet_asset, payout, _now);
      save_daily_list(bettor, bet_asset, payout, _now);

      save_bet(cur_bet_id, bettor, inviter, bet_asset, payout_list, roll_type, roll_border, roll_value, _seed, time);
      save_highodds_bet(cur_bet_id, bettor, inviter, bet_asset, payout_list, roll_type, roll_border, roll_value, _seed, time);
      save_large_bet(cur_bet_id, bettor, inviter, bet_asset, payout_list, roll_type, roll_border, roll_value, _seed, time);
      
      _trades.modify(trade_iter, 0, [&](auto& a) {
        a.in += bet_asset.amount;
        a.times += 1;
      });

      eosio::transaction r_out;
      auto t_data = make_tuple(cur_bet_id, bettor, bet_asset, payout_list, _seed, roll_type, roll_border, roll_value);
      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(receipt), t_data);
      r_out.delay_sec = 0;
      r_out.send(bettor, _self);

      // INLINE_ACTION_SENDER(dice, receipt)(_self, {_self, N(active)}, {cur_bet_id, bettor, bet_asset, payout_list, _seed, roll_type, roll_border, roll_value});
      
      if (inviter != TEAM_ACCOUNT && is_account(inviter)) {
        eosio::asset inviter_reward = eosio::asset(bet_asset.amount * INVITE_BONUS, bet_asset.symbol);

        char str[128];
        sprintf(str, "Referral reward from EOS.Win! Player: %s, Bet ID: %lld", eosio::name{bettor}.to_string().c_str(), cur_bet_id);
        INLINE_ACTION_SENDER(eosio::token, transfer)(trade_iter->contract, {_self, N(active)}, {_self, inviter, inviter_reward, string(str)} );
      }

      if (bet_asset.symbol == EOS_SYMBOL) {
        reward_game_token(bettor, inviter, bet_asset);
      }

      to_bonus_bucket(bet_asset.symbol);
    }

    /// @abi action
    void receipt(uint64_t bet_id, account_name bettor, eosio::asset bet_amt, vector<eosio::asset> payout_list, 
                    checksum256 seed, uint8_t roll_type, uint64_t roll_border, uint64_t roll_value)
    {
      require_auth(_self);
      require_recipient( bettor );
    }

    void to_jackpot(account_name bettor, eosio::asset bet_asset)
    {
      auto pos = _globals.find(GLOBAL_ID_JACKPOT_ACTIVE);

      if (pos != _globals.end() && pos->val == 0) {
        return;
      }

      if (bet_asset.symbol == EOS_SYMBOL) {
        auto jackpot_itr = _globals.find(GLOBAL_ID_MIN_JACKPOT_BET);
        if (jackpot_itr != _globals.end() && bet_asset.amount >= jackpot_itr->val) {
          eosio::asset jackpot_reward = eosio::asset(bet_asset.amount * JACKPOT_BONUS, bet_asset.symbol);
          eosio::transaction r_out;
          auto t_data = make_tuple(_self, JACKPOT_ACCOUNT, jackpot_reward, eosio::name{bettor}.to_string());
          r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, N(eosio.token), N(transfer), t_data);
          r_out.delay_sec = 1;
          r_out.send(bettor, _self);
        }
      }
    }

    string symbol_to_string(uint64_t v) const
    {
      v >>= 8;
      string result;
      while (v > 0) {
        char c = static_cast<char>(v & 0xFF);
        result += c;
        v >>= 8;
      }
      return result;
    }

    void init_player(player &a, account_name bettor, eosio::asset betin, eosio::asset payout, eosio::time_point_sec cur_time)
    {
      string symbol = symbol_to_string(betin.symbol.value);

      a.account = bettor;
      a.last_betin = betin;
      a.last_bettime = cur_time;
      a.last_payout = payout;

      uint8_t count = a.asset_items.size();
      bool finded = false;
      for (uint8_t i=0; i < count; i++)
      {
        if (a.asset_items[i].symbol == symbol)
        {
          a.asset_items[i].accu_in += betin.amount;
          a.asset_items[i].accu_out += payout.amount;
          finded = true;
        }
      }

      if (finded == false)
      {
        assetitem item;
        item.symbol = symbol;
        item.accu_in = betin.amount;
        item.accu_out = payout.amount;
        a.asset_items.push_back(item);
      }
    }

    void clear_player_accu(player &a)
    {
      uint8_t count = a.asset_items.size();
      for (uint8_t i=0; i < count; i++)
      {
        a.asset_items[i].accu_in = 0;
        a.asset_items[i].accu_out = 0;
      }
    }
    
    void save_player_list(account_name bettor, eosio::asset betin, eosio::asset payout, uint32_t now)
    {
      eosio::time_point_sec cur_time = eosio::time_point_sec( now );
      auto player_itr = _playerlists.find(bettor);

      string symbol = symbol_to_string(betin.symbol.value);
      if (player_itr == _playerlists.end()) {
        _playerlists.emplace(_self, [&](auto &a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      } 
      else
      {
        _playerlists.modify(player_itr, 0, [&](auto& a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      }
    }

    void save_rank_list(account_name bettor, eosio::asset betin, eosio::asset payout, uint32_t now)
    {
      auto start_itr = _globals.find(GLOBAL_ID_ACCU_START_TIME);
      auto start_time = 0;
      if (start_itr != _globals.end()) {
        start_time = start_itr->val;
      }

      auto stop_itr = _globals.find(GLOBAL_ID_ACCU_STOP_TIME);
      auto stop_time = 0;
      if (stop_itr != _globals.end()) {
        stop_time = stop_itr->val;
      }

      eosio::time_point_sec cur_time = eosio::time_point_sec( now );

      if (eosio::time_point_sec(start_time) > cur_time || cur_time > eosio::time_point_sec(stop_time)) {
        return;
      }
      auto player_itr = _ranklists.find(bettor);
      string symbol = symbol_to_string(betin.symbol.value);
      if (player_itr == _ranklists.end()) {
        _ranklists.emplace(_self, [&](auto &a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      } 
      else
      {
        if (eosio::time_point_sec(start_time) > player_itr->last_bettime) {
          _ranklists.modify(player_itr, 0, [&](auto& a) {
            clear_player_accu(a);
          });
        }

        _ranklists.modify(player_itr, 0, [&](auto& a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      }
    }

    void save_daily_list(account_name bettor, eosio::asset betin, eosio::asset payout, uint32_t now)
    {
      uint64_t cur_day = now / DAY_SECONDS;
      eosio::time_point_sec start_time = eosio::time_point_sec( cur_day * DAY_SECONDS );
      eosio::time_point_sec stop_time = eosio::time_point_sec( (cur_day + 1) * DAY_SECONDS);
      eosio::time_point_sec cur_time = eosio::time_point_sec( now );

      auto player_itr = _dailylists.find(bettor);
      string symbol = symbol_to_string(betin.symbol.value);
      if (player_itr == _dailylists.end()) {
        _dailylists.emplace(_self, [&](auto &a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      } 
      else
      {
        if (eosio::time_point_sec(start_time) > player_itr->last_bettime) {
          _dailylists.modify(player_itr, 0, [&](auto& a) {
            clear_player_accu(a);
          });
        }

        _dailylists.modify(player_itr, 0, [&](auto& a) {
          init_player(a, bettor, betin, payout, cur_time);
        });
      }
    }

    void init_bet(bet& a, uint64_t id, uint64_t bet_id, account_name contract, account_name bettor, account_name inviter,
                  uint64_t bet_amt, vector<eosio::asset> payout, uint8_t roll_type, uint64_t roll_border,
                  uint64_t roll_value, checksum256 seed, eosio::time_point_sec time)
    {
      a.id = id;
      a.bet_id = bet_id;
      a.contract = contract;
      a.bettor = bettor;
      a.inviter = inviter;
      
      a.bet_amt = bet_amt;
      a.payout = payout;
      a.roll_type = roll_type;
      a.roll_border = roll_border;
      a.roll_value = roll_value;
      a.seed = seed;
      a.time = time;
    }

    void save_bet( 
                  uint64_t bet_id, account_name bettor, account_name inviter, 
                  eosio::asset bet_quantity, vector<eosio::asset> payout_list, uint8_t roll_type, uint64_t roll_border, uint64_t roll_value,
                  checksum256 seed, eosio::time_point_sec time
                  )
    {
      uint64_t bet_amt = bet_quantity.amount;
      eosio::symbol_name sym_name = eosio::symbol_type(bet_quantity.symbol).name();
      auto trade_iter = _trades.find(sym_name);

      auto global_itr = _globals.find(GLOBAL_ID_HISTORY_INDEX);

      uint64_t history_index = global_itr->val % BET_HISTORY_LEN + 1;

      auto bet_itr = _bets.find(history_index);

      if (bet_itr != _bets.end())
      {
        // auto lambda_func = [&](auto& a) {};
        _bets.modify(bet_itr, 0, [&](auto& a) {
          init_bet(a, a.id, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
        });
      }
      else
      {
        _bets.emplace(_self, [&](auto &a) {
          init_bet(a, history_index, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
        });
      }

      _globals.modify(global_itr, 0, [&](auto& a) {
        a.val = history_index;
      });
    }

    void save_highodds_bet(
                  uint64_t bet_id, account_name bettor, account_name inviter, 
                  eosio::asset bet_quantity, vector<eosio::asset> payout_list, uint8_t roll_type, uint64_t roll_border, uint64_t roll_value, 
                  checksum256 seed, eosio::time_point_sec time
                  )
    {
      if ((roll_type == ROLL_TYPE_SMALL && roll_value < roll_border && roll_border < WONDER_HIGH_ODDS)
        || (roll_type == ROLL_TYPE_BIG && roll_value > roll_border && roll_border > (BET_MAX_NUM - WONDER_HIGH_ODDS))
        )
      {
        uint64_t bet_amt = bet_quantity.amount;
        eosio::symbol_name sym_name = eosio::symbol_type(bet_quantity.symbol).name();
        auto trade_iter = _trades.find(sym_name);

        auto global_itr = _globals.find(GLOBAL_ID_HIGH_ODDS_HISTORY_INDEX);

        uint64_t history_index = global_itr->val % BET_HISTORY_LEN + 1;

        auto bet_itr = _high_odds_bets.find(history_index);

        if (bet_itr != _high_odds_bets.end())
        {
          _high_odds_bets.modify(bet_itr, 0, [&](auto& a) {
            init_bet(a, a.id, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
          });
        }
        else
        {
          _high_odds_bets.emplace(_self, [&](auto &a) {
            init_bet(a, history_index, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
          });
        }

        _globals.modify(global_itr, 0, [&](auto& a) {
          a.val = history_index;
        });
      }
    }

    void save_large_bet(
                  uint64_t bet_id, account_name bettor, account_name inviter, 
                  eosio::asset bet_quantity, vector<eosio::asset> payout_list, uint8_t roll_type, uint64_t roll_border, uint64_t roll_value,
                  checksum256 seed, eosio::time_point_sec time
                  )
    {
      uint64_t bet_amt = bet_quantity.amount;
      eosio::symbol_name sym_name = eosio::symbol_type(bet_quantity.symbol).name();
      auto trade_iter = _trades.find(sym_name);
      
      if (bet_amt >= trade_iter->large_bet)
      {
        auto global_itr = _globals.find(GLOBAL_ID_LARGE_HISTORY_INDEX);

        uint64_t history_index = global_itr->val % BET_HISTORY_LEN + 1;

        auto bet_itr = _large_eos_bets.find(history_index);

        if (bet_itr != _large_eos_bets.end())
        {
          _large_eos_bets.modify(bet_itr, 0, [&](auto& a) {
            init_bet(a, a.id, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
          });
        }
        else
        {
          _large_eos_bets.emplace(_self, [&](auto &a) {
            init_bet(a, history_index, bet_id, trade_iter->contract, bettor, 
                  inviter, bet_amt, payout_list, roll_type, 
                  roll_border, roll_value, seed, time);
          });
        }

        _globals.modify(global_itr, 0, [&](auto& a) {
          a.val = history_index;
        });
      }
    }

    /// @abi action
    void verify(checksum256 seed) {
      uint64_t r = _random.gen(seed, BET_MAX_NUM);
      string str = string("Random value ") + to_string(r);
      eosio_assert(false, str.c_str());
    }

    void check_symbol(eosio::asset quantity) {
        eosio_assert(
          quantity.symbol == EOS_SYMBOL || 
          quantity.symbol == GAME_SYMBOL || 
          quantity.symbol == ADD_SYMBOL || 
          quantity.symbol == ATD_SYMBOL || 
          quantity.symbol == DAC_SYMBOL || 
          quantity.symbol == HORUS_SYMBOL || 
          quantity.symbol == IQ_SYMBOL || 
          quantity.symbol == KARMA_SYMBOL ||
          quantity.symbol == TP_SYMBOL ||
          quantity.symbol == MEET_SYMBOL ||
          quantity.symbol == BLACK_SYMBOL, 
        "Accept EOS/LUCKY/ADD/ATD/EOSDAC/HORUS/IQ/KARMA/TPT/MEETONE/BLACK only!");
    }

    void check_symbol_code(const eosio::asset& quantity) {
        eosio_assert(
          (_code == TOKEN_CONTRACT && quantity.symbol == EOS_SYMBOL) || 
          (_code == GAME_TOKEN_CONTRACT && quantity.symbol == GAME_SYMBOL) || 
          (_code == ADD_CONTRACT && quantity.symbol == ADD_SYMBOL) || 
          (_code == ATD_CONTRACT && quantity.symbol == ATD_SYMBOL) || 
          (_code == DAC_CONTRACT && quantity.symbol == DAC_SYMBOL) || 
          (_code == HORUS_CONTRACT && quantity.symbol == HORUS_SYMBOL) || 
          (_code == IQ_CONTRACT && quantity.symbol == IQ_SYMBOL) || 
          (_code == KARMA_CONTRACT && quantity.symbol == KARMA_SYMBOL) ||
          (_code == TP_CONTRACT && quantity.symbol == TP_SYMBOL) ||
          (_code == MEET_CONTRACT && quantity.symbol == MEET_SYMBOL) ||
          (_code == BLACK_CONTRACT && quantity.symbol == BLACK_SYMBOL),
        "Token do not be supported, or symbol not match with the code!");
    }

    /// @abi action
    void setriskline(eosio::asset quantity) {
        require_auth(_self);
        check_symbol(quantity);
        eosio::symbol_name sym_name = eosio::symbol_type(quantity.symbol).name();
        auto trade_iter = _trades.find(sym_name);
        if (trade_iter != _trades.end()) {
            _trades.modify(trade_iter, 0, [&](auto& info) {
                info.protect = quantity.amount;
            });
        }
    }

    /// @abi action
    void setdivi(eosio::asset quantity)
    {
      require_auth(_self);
      check_symbol(quantity);
      eosio::symbol_name sym_name = eosio::symbol_type(quantity.symbol).name();
      auto trade_iter = _trades.find(sym_name);
      if (trade_iter != _trades.end()) {
          _trades.modify(trade_iter, 0, [&](auto& info) {
              info.divi_balance = quantity.amount;
          });
      }
    }

    /// @abi action
    void setminbet(eosio::asset quantity) {
        require_auth(_self);
        check_symbol(quantity);
        eosio::symbol_name sym_name = eosio::symbol_type(quantity.symbol).name();
        auto trade_iter = _trades.find(sym_name);
        if (trade_iter != _trades.end()) {
            _trades.modify(trade_iter, 0, [&](auto& info) {
                info.min_bet = quantity.amount;
                info.large_bet = 10 * info.min_bet;
            });
        }
    }

    /// @abi action
    void luck(account_name actor, uint64_t sub) {
      require_auth(actor);

      auto g_pos = _globals.find(GLOBAL_ID_LUCK_DRAW_ACTIVE);
      eosio_assert(g_pos->val > 0, "luck is not actived");

      auto ct = current_time();

      auto nt = ct/1e6;
      auto st = sub ^ 217824523;
      eosio_assert(abs(st - nt) < 300, "wrong sub!");

      auto luck_pos = _luckers.find(actor);
      if (luck_pos == _luckers.end()) {
        luck_pos = _luckers.emplace(_self, [&](auto& info) {
          info.account = actor;
          info.last_bet_time_sec = 0;
          info.draw_time = 0;
          info.roll_value   = 0;
        });
      }
      eosio_assert(luck_pos->last_bet_time_sec + HOUR_SECONDS > nt, "do not be active player"); 
      eosio_assert(luck_pos->draw_time + luck_draw_interval < ct, "draw time has not cool down yet");

      eosio::transaction r_out;
      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(lucking), actor);
      r_out.delay_sec = 1;
      r_out.send(actor, _self);
    }

    /// @abi action
    void lucking(account_name actor) {
      require_auth(_self);
      eosio::transaction r_out;
      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(lucked), actor);
      r_out.delay_sec = 0;
      r_out.send(actor, _self);
    }

    /// @abi action
    void lucked(account_name actor) {
      require_auth(_self);

      auto ct = current_time();

      uint64_t roll_value = get_random(LUCK_DRAW_MAX);
     
      vector<eosio::asset> rewards;
      eosio::asset reward = get_luck_reward(roll_value);
      rewards.push_back(reward);

      auto total_pos = _globals.find(GLOBAL_ID_LUCK_DRAW_TOTAL);
      if (total_pos == _globals.end()) {
        total_pos = _globals.emplace(_self, [&](auto& info) {
          info.id = GLOBAL_ID_LUCK_DRAW_TOTAL;
          info.val = reward.amount;
        });
      } else {
        _globals.modify(total_pos, 0, [&](auto& info) {
          info.val += reward.amount;
        });
      }

      auto luck_pos = _luckers.find(actor);
      _luckers.modify(luck_pos, 0, [&](auto& info) {
        info.draw_time = ct;
        info.roll_value = roll_value;
      });

      eosio::transaction r_out;
      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, N(eosio.token), N(transfer), eosio::currency::transfer{_self, actor, reward, "EOS.Win lucky draw rewards!"});

      if (roll_value == 10000) {
        auto lucky_reward = eosio::asset(6660000, GAME_SYMBOL);
        rewards.push_back(lucky_reward);
        r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, GAME_TOKEN_CONTRACT, N(transfer), eosio::currency::transfer{_self, actor, lucky_reward, "EOS.Win lucky draw rewards!"});

        auto lucky_pos = _globals.find(GLOBAL_ID_LUCK_DRAW_LUCKY);
        if (lucky_pos == _globals.end()) {
          lucky_pos = _globals.emplace(_self, [&](auto& info) {
            info.id = GLOBAL_ID_LUCK_DRAW_LUCKY;
            info.val = lucky_reward.amount;
          });
        } else {
          _globals.modify(lucky_pos, 0, [&](auto& info) {
            info.val += lucky_reward.amount;
          });
        }
      }

      r_out.actions.emplace_back(eosio::permission_level{_self, N(active)}, _self, N(luckreceipt), make_tuple(actor, _seed, roll_value, rewards, luck_pos->draw_time));
      r_out.delay_sec = 0;
      r_out.send(actor, _self);
    }

    eosio::asset get_luck_reward(uint64_t roll_number) {
      if (roll_number <= 9885) {
        return eosio::asset(6, EOS_SYMBOL);
      } else if (9886 <= roll_number && roll_number <= 9985) {
        return eosio::asset(60, EOS_SYMBOL);
      } else if (9986 <= roll_number && roll_number <= 9993) {
        return eosio::asset(600, EOS_SYMBOL);
      } else if (9994 <= roll_number && roll_number <= 9997) {
        return eosio::asset(6000, EOS_SYMBOL);
      } else if (9998 <= roll_number && roll_number <= 9999) {
        return eosio::asset(60000, EOS_SYMBOL);
      } else if (roll_number == 10000) {
        return eosio::asset(160000, EOS_SYMBOL);
      } else {
        return eosio::asset(0, EOS_SYMBOL);
      }
    }

    /// @abi action
    void luckreceipt(account_name name, checksum256 seed, uint64_t roll_value, vector<eosio::asset> rewards, uint64_t draw_time) {
      require_auth(_self);
      require_recipient(name);
    }

    /// @abi action
    void luckverify(checksum256 seed) {
      uint64_t r = _random.gen(seed, LUCK_DRAW_MAX);
      string str = string("Random value ") + to_string(r);
      eosio_assert(false, str.c_str());
    }
};

#define EOSIO_ABI_EX(TYPE, MEMBERS)                                                  \
  extern "C"                                                                         \
  {                                                                                  \
    void apply(uint64_t receiver, uint64_t code, uint64_t action)                  \
		{                                                                              \
			if (code == N(eosio) && action == N(onerror))                                                  \
			{                                                                          \
				/* onerror is only valid if it is for the "eosio" code account and     \
				 * authorized by "EOS"'s "active permission */                         \
        eosio_assert(code == N(eosio), "onerror action's are only valid from " \
                         "the \"EOS\" system account");          \
			}                                                                          \
	    TYPE thiscontract(receiver);  \
      if (( code == TOKEN_CONTRACT || code == GAME_TOKEN_CONTRACT || code == ADD_CONTRACT || code == ATD_CONTRACT || \
        code == DAC_CONTRACT || code == HORUS_CONTRACT || code == IQ_CONTRACT || code == KARMA_CONTRACT || code == TP_CONTRACT || code == MEET_CONTRACT || code == BLACK_CONTRACT) \
        && (action == N(transfer))) {  \
          thiscontract.setCode(code);\
          execute_action(&thiscontract, &dice::transfer);  \
          return;  \
      } \
      if (code != receiver) return;  \
      switch (action) { \
        EOSIO_API(TYPE, MEMBERS) \
      };  \
      eosio_exit(0);                                                                         \
		}                                                                                 \
  }

EOSIO_ABI_EX(dice, (init)(setactive)(setglobal)(setnotice)(setluckrwd)(setriskline)(setdivi)(setminbet)(receipt)(verify)(start)(resolved)(luck)(lucking)(lucked)(luckreceipt)(luckverify))
