#include "lottery.hpp"

namespace eoswin {
    lottery::lottery(account_name name):
    contract(name),
    _global(_self, _self),
    _rounds(_self, _self),
    _roundfee(_self, _self),
    _game_token(TOKEN_ACCOUNT),
    _status(_self, _self),
    _dividends(_self, _self),
    _tokens(_self, _self)
    {
        _global_state = _global.exists() ? _global.get() : get_default_parameters();
        _game_token_symbol = symbol_type(GAME_SYMBOL).name();
    }

    lottery::~lottery() {
        _global.set(_global_state, _self);
    }

    global_item lottery::get_default_parameters() {
        global_item global;
        global.key_price = asset(1000, EOS_SYMBOL);  //0.1 EOS
        global.cur_round = 0;
        global.token_account = TOKEN_ACCOUNT;
        global.team_account  = TEAM_ACCOUNT;
        global.token_bonus   = 14;
        global.team_fee   = 5;
        global.drawer_fee = 1;
        global.drawer_fee_max = asset(100000, EOS_SYMBOL);
        global.active = false;
        global.mine_probability = 50;
        global.mine_team_fee    = 10;
        global.mine_referral_fee= 10;
        global.total_claimed_token_bonus = asset(0, EOS_SYMBOL);
        global.total_users = 0;
        return global;
    }

    void lottery::issue_game_token(account_name to, asset quantity, string memo) {
        if (quantity.amount <= 0) {
            return;
        }
        eosio_assert(quantity.symbol == GAME_SYMBOL, "only issue for game token");

        INLINE_ACTION_SENDER(eosio::token, transfer)(_global_state.token_account, {_global_state.token_account,N(active)}, {_global_state.token_account, to, quantity, memo} );
    }

    void lottery::get_stage_fees(const asset& bucket, uint8_t& lucky_fee, uint8_t& team_fee) {
        if (bucket.amount <= 1000000) {
            lucky_fee = 100 - _global_state.drawer_fee - _global_state.token_bonus - _global_state.team_fee;
            team_fee  = _global_state.team_fee;
        } else if (bucket.amount > 1000000 && bucket.amount <= 3000000) {
            lucky_fee = 72;
            team_fee  = 7;
        } else {
            lucky_fee = 60;
            team_fee  = 10;
        }
    }

    void lottery::active(account_name actor) {
        eosio_assert(_global_state.active == false, "lottery can't be actived twice!");
        require_auth(actor);

        SEND_INLINE_ACTION(*this, setactived, {_self,N(active)}, {true} );

        newround(actor);
    }

    void lottery::setactived(bool actived) {
        require_auth(_self);
        _global_state.active = actived;
    }

    void lottery::setstatus(uint64_t id, uint64_t val) {
        require_auth(_self);
        auto pos = _status.find(id);
        if (pos == _status.end()) {
            _status.emplace(_self, [&](auto& info) {
                info.id = id;
                info.val = val;
            });
        } else {
            _status.modify(pos, 0, [&](auto& info) {
                info.val = val;
            });
        }
    }

    void lottery::newround(account_name actor) {
        uint64_t next = _global_state.cur_round + 1;
        eosio_assert(next > _global_state.cur_round, "new round number is smaller than or equal with the old!");

        asset next_bucket = asset(0, EOS_SYMBOL);
        auto rnd_pos = _rounds.find( _global_state.cur_round);
        auto fee_pos = _roundfee.find(_global_state.cur_round);
        if (rnd_pos != _rounds.end() && fee_pos != _roundfee.end() && rnd_pos->lucky_player <= 0) {
            next_bucket = fee_pos->to_lucky;
        }
        
        _rounds.emplace(actor, [&](round_item& info) {
            info.round         = next;
            info.bills_num     = 0;
            info.last_key      = 0;
            info.reward_bucket = next_bucket;
            info.start_time    = current_time();
            info.draw_time     = 0;
        });
        
        _global_state.cur_round = next;
    }

    void lottery::endround() {
        round_item rnd = _rounds.get( _global_state.cur_round );

        uint8_t lucky_fee, team_fee;
        get_stage_fees(rnd.reward_bucket, lucky_fee, team_fee);

        auto to_drawer_max  = rnd.reward_bucket * _global_state.drawer_fee / 100;
        to_drawer_max = to_drawer_max > _global_state.drawer_fee_max ? _global_state.drawer_fee_max : to_drawer_max;

        bills_table bills(_self, _global_state.cur_round);
        auto idx = bills.template get_index<N(byplayer)>();
        auto itr = idx.lower_bound( rnd.draw_account );
        eosio_assert(itr != idx.end(), "join game first");
        uint64_t keynum = 0;
        for(; itr != idx.end(); itr++) {
            if (itr->referal == ACTIVITY_ACCOUNT) {
                continue;
            }
            if (itr->player == rnd.draw_account ) {
                keynum += itr->high - itr->low + 1;
            } else {
                break;
            }
        }
        auto to_drawer = _global_state.key_price * keynum / 2;
        to_drawer = to_drawer > to_drawer_max ? to_drawer_max : to_drawer;

        auto to_lucky = rnd.reward_bucket * lucky_fee / 100;
        auto to_team  = rnd.reward_bucket * team_fee / 100;
        auto to_tokener = rnd.reward_bucket - to_team - to_lucky  - to_drawer;

        INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {_self,N(active)}, {_self, DIVI_ACCOUNT, to_tokener, "[eos.win] To EOS.Win Bonus Pool [https://eos.win/lottery]"} );

        INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {_self,N(active)}, {_self, _global_state.team_account, to_team, "[eos.win] Team fee"} );

        transaction to_drawer_tx;
        to_drawer_tx.actions.emplace_back(eosio::permission_level{_self, N(active)}, N(eosio.token), N(transfer), currency::transfer{_self, rnd.draw_account, to_drawer, "[eos.win] Round drawer reward"});
        to_drawer_tx.delay_sec = 1;
        to_drawer_tx.send(rnd.draw_account, _self);

        if ( rnd.lucky_player > 0 ) {
            char buffer[256];
            sprintf(buffer,"[eosluckygame@eos.win][round: %lld][lucky key: %lld] winner reward", _global_state.cur_round, rnd.lucky_key);
            string notify = buffer;
            INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {_self,N(active)}, {_self, rnd.lucky_player, to_lucky, notify} );
        }
        
        _roundfee.emplace(_self, [&](auto& info) {
            info.round = _global_state.cur_round;
            info.to_team = to_team;
            info.to_bonus = to_tokener;
            info.to_drawer = to_drawer;
            info.to_lucky  = to_lucky;
        });
    }

    void lottery::transfer(account_name from, account_name to) {
        const auto params = eosio::unpack_action_data<eosio::currency::transfer>();

        if (params.from == _self || params.to != _self) {
            return;
        }

        if (params.memo == "deposit") {
            return;
        } else if (params.memo == "activity") {
            bills_table bills(_self, _global_state.cur_round);
            bills.emplace(_self, [&](bill_item& info) {
                info.id = bills.available_primary_key();
                info.player = from;
                info.low    = params.quantity.amount;
                info.high   = 0;
                info.mine_token = asset(0, GAME_SYMBOL);
                info.referal = ACTIVITY_ACCOUNT;
                info.time = current_time();
            });

            auto pos = _rounds.find(_global_state.cur_round);
            _rounds.modify(pos, 0, [&](round_item& info) {
                info.reward_bucket += params.quantity;
            });
            return;
        }

        eosio_assert(_global_state.active == true, "lottery not activated yet!");

        eosio_assert(params.quantity.symbol == EOS_SYMBOL, "accept EOS only");
        eosio_assert(params.quantity.is_valid(), "transfer invalid quantity");
		eosio_assert(params.quantity.amount >= _global_state.key_price.amount, "amount can't be smaller than the key price");

        bill_item bill = buykeys(params.from, params.quantity, params.memo);

        auto pos = _rounds.find(_global_state.cur_round);
        eosio_assert(pos != _rounds.end(), "round number not found");

        _rounds.modify(pos, 0, [&](round_item& info) {
            info.reward_bucket += params.quantity;
            info.bills_num     += 1;
        });
    }

    bill_item lottery::buykeys(account_name buyer, asset quantity, string memo) {
        string referal_name = memo;
        bool has_referal = referal_name == "" ? false : true;
        account_name referal = has_referal ? eosio::string_to_name(referal_name.c_str()) : 0;

        bill_item bill;
        bill.player  = buyer;
        bill.referal = referal;
        
        auto rnd = _rounds.find(_global_state.cur_round);
        eosio_assert(rnd != _rounds.end(), "round number can't be found in rounds table error!"); 

        uint64_t num = static_cast<uint64_t>(quantity.amount / _global_state.key_price.amount);
        eosio_assert(num * _global_state.key_price.amount == quantity.amount, "transfer number must be integer multiples of key price");

        bill.low = rnd->last_key + 1 + num;
        eosio_assert(bill.low > rnd->last_key, "low key is out of range while buy keys");

        bill.high = bill.low + num - 1;
        eosio_assert(bill.high > rnd->last_key, "high key is out of range while buy keys");

        asset surprise = asset(mine(num), GAME_SYMBOL);

        asset to_referal_token = asset(0, GAME_SYMBOL);
        if (referal > 0) {
            eosio_assert(buyer != referal, "referal can't be self!");
            to_referal_token = surprise * _global_state.mine_referral_fee / 100;
        }
        bill.mine_token = surprise * (100 - _global_state.mine_referral_fee - _global_state.mine_team_fee) / 100;
        asset to_team_token = surprise - to_referal_token - bill.mine_token;

        issue_game_token(_global_state.team_account, to_team_token, "[lottery@eos.win] LUCKY token for dev team");
        issue_game_token(referal, to_referal_token, "[lottery@eos.win] LUCKY token for referal");
        issue_game_token(buyer, bill.mine_token, "[lottery@eos.win] LUCKY token for player");

        bill.time = current_time();

        bills_table bills(_self, _global_state.cur_round);
        bill.id = bills.available_primary_key();
        bills.emplace(_self, [&](bill_item& info) {
            info = bill;
        });

        players_table players(_self, _global_state.cur_round);
        auto pos = players.find(buyer);
        if (pos != players.end()) {
            players.modify(pos, 0, [&](player_item& info) {
                info.bills_num += 1;
                eosio_assert(info.bills_num <= max_player_bills_per_round, "max 500 bills for one player per round");
            });
        } else {
            players.emplace(_self, [&](player_item& info) {
                info.player = buyer;
                info.bills_num = 1;
            });
        }

        _rounds.modify(rnd, 0, [&](round_item& info) {
            info.last_key = bill.high;
        });

        _global_state.total_users += 1;

        return bill;
    }

    uint64_t lottery::mine(uint64_t keynum) {
        eosio_assert(_global_state.active == true, "lottery game has not began!");

        uint64_t mine_token = keynum*7500;

        auto left_token = _game_token.get_balance(TOKEN_ACCOUNT, _game_token_symbol);
        mine_token = mine_token > left_token.amount ? left_token.amount : mine_token;

        return mine_token;
    }

    uint64_t lottery::randomkey(uint64_t max) {
        checksum256 result;
        static uint64_t seed = static_cast<uint64_t>((int)&result);
        auto rnd = _rounds.find(_global_state.cur_round);
		auto mixedBlock = tapos_block_prefix() + tapos_block_num() + rnd->reward_bucket.amount + seed + current_time();
        
        seed += (mixedBlock >> 33);

        const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
		sha256((char *)mixedChar, sizeof(mixedChar), &result);
		const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&result);

        eosio_assert(max > 0, "random max must > 0");
		return (p64[1] % max) + 1;
    }

    void lottery::delaydraw(account_name drawer) {
        require_auth(drawer);

        eosio_assert(_global_state.active == true, "lottery has not been actived!");

        auto pos = _rounds.find(_global_state.cur_round);
        eosio_assert(pos != _rounds.end(), "round is missing!");
        eosio_assert(pos->last_key > 0, "none buy keys");

        auto ct = current_time();
        eosio_assert(ct > (pos->start_time + useconds_draw_interval), "draw time has not cool down!");

        players_table players(_self, _global_state.cur_round);
        players.get(drawer, "join game first!");

        transaction dr;
        dr.actions.emplace_back( permission_level{ _self, N(active) }, _self, N(drawing), drawer);
        dr.delay_sec = 1;
        dr.send( _self, _self);
    }

    void lottery::drawing(account_name drawer) {
        require_auth(_self);

        transaction dr;
        dr.actions.emplace_back( permission_level{ _self, N(active) }, _self, N(draw), drawer);
        dr.delay_sec = 1;
        dr.send( _self, _self);
    }

    void lottery::draw(account_name drawer) {
        require_auth(_self);

        auto pos = _rounds.find(_global_state.cur_round);

        auto ct = current_time();

        uint64_t lucky_key = randomkey(pos->last_key);

        bills_table bills(_self, _global_state.cur_round);
        auto it = bills.begin(); 
        for (; it!=bills.end(); it++) {
            if (it->referal == ACTIVITY_ACCOUNT) {
                continue;
            }
            if (lucky_key >= it->low && lucky_key <= it->high) {
                break;
            }
        }

        _rounds.modify(pos, 0, [&](round_item& info) {
            info.draw_account = drawer;
            info.draw_time = ct;
            info.lucky_key = lucky_key;

            if (it == bills.end()) {
                info.lucky_player = 0;
            } else {
                info.lucky_player = it->player;
            }
            
        });
        
        endround();
        newround(_self);

        erasehistory(_global_state.cur_round - HISTORY_NUM, 1);

    }

    void lottery::claimbonus(account_name actor, account_name owner) {
        require_auth(actor);
        
        auto b_pos = _status.find(STATUS_ID_BONUS_ACTIVE);
        eosio_assert(b_pos->val == true, "bonus has been locked!");
        auto ct = current_time();

        auto dividend_base_time = _status.get(STATUS_ID_CLAIM_PEROID_BASE_TIME, "can not find claim base time");
        auto devidend_acc_interval = _status.get(STATUS_ID_CLAIM_PEROID_ACC_INTERVAL, "can not find accumulate interval");
        auto devidend_claim_interval = _status.get(STATUS_ID_CLAIM_PEROID_CLAIM_INTERVAL, "can not find claim interval");
        uint64_t period = devidend_acc_interval.val + devidend_claim_interval.val;
        uint64_t n = 0;
        uint64_t ct_sec = ct / uint64_t(1e6);
        if (ct_sec > dividend_base_time.val) {
            n = (ct_sec - dividend_base_time.val) / period;
        }
        auto acc_start = dividend_base_time.val + n * period;
        auto claim_start   = acc_start + devidend_acc_interval.val;
        auto claim_end = claim_start + devidend_claim_interval.val;

        char buffer[128];
        sprintf(buffer, "%lld is not in claim time [%lld, %lld], base time: %lld", ct_sec, claim_start, claim_end, dividend_base_time.val);
        eosio_assert(ct_sec >= claim_start && ct_sec <= claim_end, buffer);

        auto last_claimed_time = _game_token.get_age(owner, _game_token_symbol);
        eosio_assert(ct > last_claimed_time + useconds_claim_token_bonus_interval, "claim token bonus time has not cool down yet!");

        auto snapshot_pos = _status.find(STATUS_ID_TOKEN_SNAPSHOT_TIME);
        if (snapshot_pos == _status.end()) {
            snapshot_pos = _status.emplace(_self, [&](auto& info) {
                info.id  = STATUS_ID_TOKEN_SNAPSHOT_TIME;
                info.val = 0;
            });
        }
        if (snapshot_pos->val < claim_start) {
            snapshot_token_balance();

            _status.modify(snapshot_pos, 0, [&](auto& info) {
                info.val = claim_start;
            });
        }

        asset balance = _game_token.get_balance(TOKEN_ACCOUNT, _game_token_symbol);
        asset max_supply = _game_token.get_max_supply(_game_token_symbol);
        eosio_assert(balance.amount < max_supply.amount, "has no game token issued!");

        asset b = _game_token.get_balance(owner, _game_token_symbol);
        eosio_assert(b.amount > 0, "has no game token!");

        asset supply = max_supply - balance;
        double percent = (double)b.amount / supply.amount;

        auto to_bonus = divide_token(owner, TOKEN_CONTRACT, EOS_SYMBOL, percent);
        auto to_lucky_bonus = divide_token(owner, GAME_TOKEN_CONTRACT, GAME_SYMBOL, percent);
        auto to_add_bonus = divide_token(owner, ADD_CONTRACT, ADD_SYMBOL, percent);
        auto to_atd_bonus = divide_token(owner, ATD_CONTRACT, ATD_SYMBOL, percent);
        auto to_dac_bonus = divide_token(owner, DAC_CONTRACT, DAC_SYMBOL, percent);
        auto to_horus_bonus = divide_token(owner, HORUS_CONTRACT, HORUS_SYMBOL, percent);
        auto to_iq_bonus = divide_token(owner, IQ_CONTRACT, IQ_SYMBOL, percent);
        auto to_karma_bonus = divide_token(owner, KARMA_CONTRACT, KARMA_SYMBOL, percent);
        auto to_meet_bonus = divide_token(owner, MEET_CONTRACT, MEET_SYMBOL, percent);
        auto to_black_bonus = divide_token(owner, BLACK_CONTRACT, BLACK_SYMBOL, percent);

        asset fee = asset(b.amount*0.005, GAME_SYMBOL);
    
        INLINE_ACTION_SENDER(eosio::token, updateage)( _global_state.token_account, {_global_state.token_account, N(active)}, {owner, asset(0, GAME_SYMBOL), ct} );

        _global_state.total_claimed_token_bonus += to_bonus;

        auto count_pos = _status.find(STATUS_ID_CLAIM_BONUS_COUNT);
        uint64_t claim_count = 1;
        if (count_pos == _status.end()) {
            _status.emplace(_self, [&](auto& info) {
                info.id = STATUS_ID_CLAIM_BONUS_COUNT;
                info.val = claim_count;
            });
            count_pos = _status.find(STATUS_ID_CLAIM_BONUS_COUNT);
        } else {
            claim_count = count_pos->val + 1;
            _status.modify(count_pos, 0, [&](auto& info) {
                info.val = claim_count;
            });
        }

        if (claim_count > CLAIM_BONUS_HISTORY_NUM) {
            _dividends.erase(_dividends.begin());
        }

        _dividends.emplace(_self, [&](auto& info) {
            info.id = claim_count;
            info.player = owner;
            info.time   = ct;

            if (to_bonus.amount > 0) {
                info.bonus.push_back(to_bonus);
            }
            if (to_lucky_bonus.amount > 0) {
                info.bonus.push_back(to_lucky_bonus);
            }
            if (to_add_bonus.amount > 0) {
                info.bonus.push_back(to_add_bonus);
            }
            if (to_atd_bonus.amount > 0) {
                info.bonus.push_back(to_atd_bonus);
            }
            if (to_dac_bonus.amount > 0) {
                info.bonus.push_back(to_dac_bonus);
            }
            if (to_horus_bonus.amount > 0) {
                info.bonus.push_back(to_horus_bonus);
            }
            if (to_iq_bonus.amount > 0) {
                info.bonus.push_back(to_iq_bonus);
            }
            if (to_karma_bonus.amount > 0) {
                info.bonus.push_back(to_karma_bonus);
            }
            if (to_meet_bonus.amount > 0) {
                info.bonus.push_back(to_meet_bonus);
            }
            if (to_black_bonus.amount > 0) {
                info.bonus.push_back(to_black_bonus);
            }
        });
    }

    void lottery::eraseclaims() {
        require_auth(_self);
        claims_table claims(_self, _self);
        for(auto itr=claims.begin(); itr != claims.end(); ) {
            _dividends.emplace(_self, [&](auto& info) {
                info.id = itr->id;
                info.player = itr->player;
                info.time = itr->time;
                info.bonus.push_back(itr->bonus);
            });

            itr = claims.erase(itr);
        }
    }

    asset lottery::divide_token(account_name to, account_name token_account, symbol_name sym, const double percent) {
        auto pos = _status.find(STATUS_ID_CLAIM_SUB_TOKEN_ACTIVE);
        if (sym != EOS_SYMBOL && (pos == _status.end() || pos->val != true )) {
            return asset(0, sym);
        }

        symbol_name sname = symbol_type(sym).name();
        auto token_pos = _tokens.find(sname);
        if (token_pos == _tokens.end()) {
            return asset(0, sym);;
        }

        asset to_dividend = asset(token_pos->balance.amount * percent, sym);
        if (to_dividend.amount <= 0) {
            return to_dividend;
        }

        INLINE_ACTION_SENDER(eosio::token, transfer)( token_account, {DIVI_ACCOUNT, N(active)}, {DIVI_ACCOUNT, to, to_dividend, "[eos.win] Claim token dividend."} );

        return to_dividend;
    }

    void lottery::snapshot_token_balance() {
        uint64_t data[10][2] = {
            {TOKEN_CONTRACT, EOS_SYMBOL},   {GAME_TOKEN_CONTRACT, GAME_SYMBOL},
            {ADD_CONTRACT, ADD_SYMBOL},     {ATD_CONTRACT, ATD_SYMBOL},
            {DAC_CONTRACT, DAC_SYMBOL},     {HORUS_CONTRACT, HORUS_SYMBOL},
            {IQ_CONTRACT, IQ_SYMBOL},       {KARMA_CONTRACT, KARMA_SYMBOL},
            {BLACK_CONTRACT, BLACK_SYMBOL}, {MEET_CONTRACT, MEET_SYMBOL}
        };
        for (int i=0; i<10; i++) {
            eosio::token tokener(data[i][0]);
            symbol_name sname = symbol_type(data[i][1]).name();
            uint64_t b_amount = tokener.get_balance_amount(DIVI_ACCOUNT, sname);
            auto token_pos = _tokens.find(sname);
            if (token_pos == _tokens.end()) {
                _tokens.emplace(_self, [&](auto& info) {
                    info.balance = asset(b_amount, data[i][1]);
                });
            } else {
                _tokens.modify(token_pos, 0, [&](auto& info) {
                    info.balance = asset(b_amount, data[i][1]);
                });
            }
        }
    }

    void lottery::activesub(bool active) {
        require_auth(_self);
        auto pos = _status.find(STATUS_ID_CLAIM_SUB_TOKEN_ACTIVE);
        if(pos == _status.end()) {
            _status.emplace(_self, [&](auto& info) {
                info.id = STATUS_ID_CLAIM_SUB_TOKEN_ACTIVE;
                info.val = active;
            });
        } else {
            _status.modify(pos, 0, [&](auto& info) {
                info.val = active;
            });
        }
    }

    void lottery::activebonus(bool active) {
        require_auth(_self);
        auto pos = _status.find(STATUS_ID_BONUS_ACTIVE);
        if(pos == _status.end()) {
            _status.emplace(_self, [&](auto& info) {
                info.id = STATUS_ID_BONUS_ACTIVE;
                info.val = active;
            });
        } else {
            _status.modify(pos, 0, [&](auto& info) {
                info.val = active;
            });
        }
    }

    void lottery::erasehistory(uint64_t offset, uint64_t limit) {
        if (_global_state.cur_round <= HISTORY_NUM) { 
           return;
        }

        auto max_round = _global_state.cur_round - HISTORY_NUM;
        for (auto itr = _rounds.begin(); itr->round <= max_round; ) {
            itr = _rounds.erase(itr);
        }

        for (auto fee_itr = _roundfee.begin(); fee_itr != _roundfee.end() && fee_itr->round <= max_round; ) {
            fee_itr = _roundfee.erase(fee_itr);
        }

        eosio_assert(offset > 0 && limit > 0, "offset > 0 && limit > 0.");
        auto temp_round = offset + limit - 1;
        temp_round = temp_round < max_round ? temp_round : max_round;
        while(temp_round >= offset) {
            bills_table bills(_self, temp_round);

            for(auto bill_itr = bills.begin(); bill_itr != bills.end();) {
                bill_itr = bills.erase(bill_itr);
            }

            players_table players(_self, temp_round);
            for(auto player_itr = players.begin(); player_itr != players.end();) {
                player_itr = players.erase(player_itr);
            }

            temp_round -= 1;
        }

    }
};


#define eosio_ABI_EX(TYPE, MEMBERS)                                                    \
	extern "C"                                                                         \
	{                                                                                  \
		void apply(uint64_t receiver, uint64_t code, uint64_t action)                  \
		{                                                                              \
			if (action == N(onerror))                                                  \
			{                                                                          \
				/* onerror is only valid if it is for the "eosio" code account and     \
				 * authorized by "EOS"'s "active permission */                         \
				eosio_assert(code == N(eosio), "onerror action's are only valid from " \
											   "the \"EOS\" system account");          \
			}                                                                          \
			auto self = receiver;                                                      \
            if (code == N(eosio.token) && action == N(transfer)) {                     \
                TYPE thiscontract(self);                                               \
                eosio::execute_action( &thiscontract, &eoswin::lottery::transfer );  \
            }                                                                          \
                                                                                       \
            if (code == self || action == N(onerror))                                  \
			{                                                                          \
				TYPE thiscontract(self);                                               \
				switch (action)                                                        \
				{                                                                      \
					EOSIO_API(TYPE, MEMBERS)                                           \
				}                                                                      \
				/* does not allow destructor of thiscontract to run: enu_exit(0); */   \
			}                                                                          \
		}                                                                              \
	}

eosio_ABI_EX( eoswin::lottery, (delaydraw)(drawing)(draw)(active)(setactived)(claimbonus)(activesub)(activebonus)(eraseclaims)(setstatus))
