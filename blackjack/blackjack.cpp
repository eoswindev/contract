#include "blackjack.hpp"

namespace eoswin {

	blackjack::blackjack(account_name name):
    contract(name),
    _globals(_self, _self),
    _tradetokens(_self, _self),
    _games(_self, _self),
    _players(_self, _self),
	_history(_self, _self) {
		seed();
	}

    blackjack::~blackjack() {}

	void blackjack::init() {
        require_auth(_self);

        setglobal(G_ID_ACTIVE, 0);
        setglobal(G_ID_GAME_ID, 0);
		setglobal(G_ID_BANKER_ACTIVE, 0);
		setglobal(G_ID_BANKER_NUM, 0);
		setglobal(G_ID_HISTORY_SIZE, 0);
		setglobal(G_ID_LUCKY_FACTOR, 25000);
		setglobal(G_ID_COMMON_WIN_COUNT, 0);
		setglobal(G_ID_DOUBLE_WIN_COUNT, 0);
		setglobal(G_ID_LOSE_COUNT, 0);
		setglobal(G_ID_PUSH_COUNT, 0);
		setglobal(G_ID_DIVIDE_RATIO, 25);

		setglobal(G_ID_ACTIVITY_START_TIME, 1541134800*1e6);
		setglobal(G_ID_ACTIVITY_END_TIME, 1541142000*1e6);

		setglobal(G_ID_DIVIDE_INTERVAL, 2*3600*1e6);

        settoken(asset(20000000, EOS_SYMBOL), EOS_ACCOUNT, asset(2500, EOS_SYMBOL), false, asset(50000000, EOS_SYMBOL), now());
	}

	void blackjack::seed() {
		
		auto s = transaction_size();
		char *tx = (char *)malloc(s);
		read_transaction(tx, s);
		checksum256 txid;
		sha256(tx, s, &txid);
	
		checksum256 sseed;
		sseed = _random.create_sys_seed(0);
		
		_random.seed(sseed, txid);

	}

    uint8_t blackjack::cal_points(const vector<uint8_t>& cards) {
        uint8_t points = 0;
        uint8_t count_A = 0;
        uint8_t card = 0;
        for (int i = 0; i < cards.size(); i++) {
            if (cards[i] >= 104) {
                continue;
            }

            card = cards[i];
            if (is_A(card)) {
                count_A += 1;
            } else if (is_ten(card)) {
                points += 10;
            } else {
                points += (card % 13 + 1);
            }
        }

        eosio_assert(count_A <= 8, "card A num can't large than 4!");
        uint8_t result = points;
        if (count_A > 0) {
            uint8_t temp = 0;
			uint8_t min = 0;
            for (int i = 0; i < 2; i++) {
                temp = points + A[count_A - 1][i];
                if (temp > result && temp <= GAME_MAX_POINTS) {
                    result = temp;
                }

				if (min == 0 || temp < min) {
					min = temp;
				}
            }

			if (result == points) {
				result = min;
			}
        }

        return result;
    }

    uint8_t blackjack::random_card(const game_item& gm, const vector<uint8_t>& exclude) {
		map<uint8_t, uint8_t> used;
		for (size_t i = 0; i < gm.banker_cards.size(); i++) {
			used[gm.banker_cards[i]] = 1;
		}

		for (size_t i = 0; i < gm.player_cards.size(); i++) {
			used[gm.player_cards[i]] = 1;
		}

		for (size_t i = 0; i < exclude.size(); i++) {
			used[exclude[i]] = 1;
		}

		vector<uint8_t> cards;
		for (uint8_t num = 0; num < 104; num++) {
			if (used[num] == 1) {
				continue;
			}
			cards.push_back(num);
		}

		uint8_t  card = CARD_UNKOWN;
		if (cards.size() > 0) {
			size_t idx  = (size_t)_random.generator((uint64_t)cards.size());
			card = cards[idx];
		}

		return card;
	}

	void blackjack::deal_to_banker(game_item& gm) {

		uint8_t banker_points = cal_points(gm.banker_cards);
		uint8_t player_points = cal_points(gm.player_cards);
		vector<uint8_t> ex_cards;
		while(!(banker_points >= 17 && banker_points >= player_points)) {
			auto card = random_card(gm, ex_cards);
			gm.banker_cards.push_back(card);

			banker_points = cal_points(gm.banker_cards);
		}
	}

	void blackjack::replace_banker_first_card(game_item& gm) {
		auto itr = gm.banker_cards.begin();
		gm.banker_cards.erase(itr);

		vector<uint8_t> ex_cards;
		if (gm.status == GAME_STATUS_STANDED && gm.manual_stand == true ) {
			auto banker_open_card = gm.banker_cards[0];
			if (is_A(banker_open_card)) {
				for (uint8_t i=0; i<104; i++) {
					if (is_ten(i)) {
						ex_cards.push_back(i);
					}
				}
			} else if (is_ten(banker_open_card)) {
				for (uint8_t i=0; i<104; i++) {
					if (is_A(i)) {
						ex_cards.push_back(i);
					}
				}
			}
			
		}

		auto card = random_card(gm, ex_cards);
		gm.banker_cards.insert(gm.banker_cards.begin(), card);
	}

	bool blackjack::is_A(uint8_t card) {
		return (card % 13) == 0;
	}

	bool blackjack::is_ten(uint8_t card) {
		return (card % 13) >= 9;
	}

	asset blackjack::asset_from_vec(const vector<asset>& vec, symbol_type sym) const {
		asset ret = asset(0, sym);
		for (size_t i=0; i<vec.size(); i++) {
			if (vec[i].symbol == sym) {
				ret = vec[i];
				break;
			}
		}

		return ret;
	}

	account_name blackjack::token_account_for_symbol(symbol_type sym) const {
		account_name act = 0;
		if (sym == EOS_SYMBOL) {
			act = EOS_ACCOUNT;
		}

		return act;
	}

	void blackjack::assert_not_operation_if_need_insure(const game_item& gm) {
		if (gm.deal_num == 1 && gm.banker_cards.size() == 2 && is_A(gm.banker_cards[1])) {
			eosio_assert(gm.insure_status == 1, "need choose insure Yes or No first!");
		}
	}

	string blackjack::card_to_string(uint8_t card) {
		uint8_t suit  = card / 13;
		uint8_t point = card % 13;
		string suit_str;
		if (suit == 0 || suit == 4) {
			suit_str = "♦";
		} else if (suit == 1 || suit == 5) {
			suit_str = "♥";
		} else if (suit == 2 || suit == 6) {
			suit_str = "♠";
		} else if (suit == 3 || suit == 7) {
			suit_str = "♣";
		} else {
			suit_str = "unknown";
		}

		string point_str;
		if (point == 0) {
			point_str = "A";
		} else if (point == 10) {
			point_str = "J";
		} else if (point == 11) {
			point_str = "Q";
		} else if (point == 12) {
			point_str = "K";
		} else {
			point_str = to_string(point+1);
		}

		string ret = suit_str + point_str;
		return ret;
	}

	string blackjack::cards_to_string(const vector<uint8_t>& cards) {
		string ret;
		size_t len = cards.size();
		for (size_t i=0; i<len; i++) {
			ret += card_to_string(cards[i]);
			if (i + 1 < len) {
				ret += ";";
			}
		}

		return ret;
	}

	void blackjack::reward_game_token(const game_item& gm) {
		auto factor_pos = _globals.find(G_ID_LUCKY_FACTOR);

		auto reward = asset(gm.bet.amount * ((double)factor_pos->val / 10000), GAME_SYMBOL);
		if (reward.amount <= 0) {
			return;
		}
		token gtk(GAME_TOKEN_ACCOUNT);
		auto balance = gtk.get_balance(GAME_TOKEN_ACCOUNT, symbol_type(GAME_SYMBOL).name());
		reward = reward > balance ? balance : reward;

		auto to_player = reward * PLAYER_TOKEN_FEE / 10;
		auto to_team   = reward - to_player;
		if (gm.referal != TEAM_ACCOUNT) {
			auto to_inviter = reward * INVITER_TOKEN_FEE / 10;
			if (to_inviter.amount > 0 && is_account(gm.referal)) {
				to_team.set_amount(to_team.amount - to_inviter.amount);
				INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_ACCOUNT, {GAME_TOKEN_ACCOUNT,N(active)}, {GAME_TOKEN_ACCOUNT, gm.referal, to_inviter, "LUCKY token for inviter [https://eos.win]"} );
			}
		}

		if (to_player.amount > 0) {
			INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_ACCOUNT, {GAME_TOKEN_ACCOUNT,N(active)}, {GAME_TOKEN_ACCOUNT, gm.player, to_player, "LUCKY token for player [https://eos.win]"} );
		}

		if (to_team.amount > 0) {
			INLINE_ACTION_SENDER(eosio::token, transfer)(GAME_TOKEN_ACCOUNT, {GAME_TOKEN_ACCOUNT,N(active)}, {GAME_TOKEN_ACCOUNT, TEAM_ACCOUNT, to_team, "LUCKY token for team [https://eos.win]"} );
		}
    }

	void blackjack::to_dividend(symbol_type sym) {
		symbol_name sym_name = symbol_type(sym).name();
		auto trade_iter = _tradetokens.find(sym_name);
		auto interval_pos = _globals.find(G_ID_DIVIDE_INTERVAL);

		if (trade_iter == _tradetokens.end() || interval_pos == _globals.end()) {
			return;
		}
		auto ct = current_time();

		if (ct < (trade_iter->divide_time + interval_pos->val)) {
			return;
		}

		auto ratio_pos = _globals.find(G_ID_DIVIDE_RATIO);

		token tk(trade_iter->token_account);
		auto balance_amount = tk.get_balance_amount(_self, sym_name);
		uint64_t dividends_amount = 0;
		if (trade_iter->divide_balance.amount < balance_amount) {
			dividends_amount = (balance_amount - trade_iter->divide_balance.amount) * ratio_pos->val / 100;
		}

		if (dividends_amount <= 10000) {
			return;
		}

		_tradetokens.modify(trade_iter, 0, [&](auto& info) {
			info.divide_balance = asset(balance_amount - dividends_amount, sym);
			info.divide_time = ct;
		});
		INLINE_ACTION_SENDER(eosio::token, transfer)(trade_iter->token_account, {_self,N(active)}, {_self, DIVI_ACCOUNT, asset(dividends_amount, sym), "To EOS.Win Bonus Pool [https://eos.win/blackjack]"} );
    }

	void blackjack::transfer(account_name from, account_name to, asset quantity, string memo) {

        if (memo == "deposit" || from == N(eosio.stake)) {
			auto tk_pos = _tradetokens.find(quantity.symbol.name());
			if (tk_pos != _tradetokens.end()) {
				_tradetokens.modify(tk_pos, 0, [&](auto& info) {
					info.risk_line += quantity;
					info.divide_balance += quantity;
				});
			}
            return;
        }

        if (from == _self || to != _self) {
            return;
        }

        auto active_pos = _globals.find(G_ID_ACTIVE);
        eosio_assert(active_pos->val == 1, "game is not active!");

		auto tradetoken = _tradetokens.get(quantity.symbol.name(), "game do not support the token transfered");

        vector<string> pieces;
        boost::algorithm::split(pieces, memo, boost::algorithm::is_any_of(","));

        string action_type = pieces[0];
		eosio_assert(action_type == "play" || action_type == "banker", "memo is incorrect");
        if (action_type == "play") {
            eosio_assert(pieces.size() == 4, "memo for play is incorrect");

            account_name banker = string_to_name(pieces[1].c_str());
			asset max_bet = asset(0, quantity.symbol);
            if (banker == _self) {
                token acts(tradetoken.token_account);
                asset balance = acts.get_balance(_self, tradetoken.risk_line.symbol.name());
                eosio_assert(balance > tradetoken.risk_line, "balance reached the risk line");

				max_bet.set_amount(balance.amount * MAX_BET_FEE);
            } else {
				eosio_assert(false, "player banker is not active!");
            }

			string action_name = pieces[2];

            account_name referal = TEAM_ACCOUNT;
            if (!pieces[3].empty()) {
                referal = string_to_name(pieces[3].c_str());
				eosio_assert(from != referal, "inviter can't be self");
                eosio_assert(is_account(referal), "referal does not exist");
            }

			if (action_name == "new") {
				eosio_assert(quantity >= tradetoken.min, "can't less than min bet asset");
				eosio_assert(quantity <= max_bet, "can't larger than max bet asset");
				new_game(from, banker, quantity, referal);
			} else if (action_name == "double") {
				double_down(from, quantity);
			} else if (action_name == "insure") {
				insure(from, quantity);
			} else {
				eosio_assert(false, "unkown action to play");
			}
        } else {
			eosio_assert(false, "unkown action type");
        }	
    }

	void blackjack::new_game(account_name player, account_name banker, asset& bet, account_name referal) {

		auto pos = _games.find(player);
		eosio_assert(pos == _games.end() || pos->status == GAME_STATUS_CLOSED , "your last game is in progress!");
		
        auto game_id_pos = _globals.find(G_ID_GAME_ID);
		auto ct = current_time();

        game_item gm;
        gm.game_id = game_id_pos->val + 1;
        gm.start_time = ct;
		gm.close_time = 0;
		gm.deal_num   = 0;
        gm.banker  = banker;

        gm.player  = player;
        gm.referal = referal;
        gm.bet     = bet;
		gm.insure  = asset(0, bet.symbol);
		gm.insure_status = 0;
        gm.status  = GAME_STATUS_BEGAN;

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("new"), gm.game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(gm.player, _self);

		for (auto divide_pos=_tradetokens.begin(); divide_pos!=_tradetokens.end(); divide_pos++) {
			if (divide_pos->divide_open == true) {
				to_dividend(divide_pos->risk_line.symbol);
			}
		}

		if (pos == _games.end()) {
			_games.emplace(_self, [&](auto& info) {
				info = gm;
			});
		} else {
			_games.modify(pos, 0, [&](auto& info) {
				info = gm;
			});
		}
        
        _globals.modify(game_id_pos, 0, [&](auto& info) {
            info.val = gm.game_id;
        });

		if (banker == _self) {
			auto token_pos = _tradetokens.find(bet.symbol.name());
			_tradetokens.modify(token_pos, 0, [&](auto& info) {
				info.in += bet;
				info.play_times += 1;
			});
		} else {

		}

		if (bet.symbol == EOS_SYMBOL) {
			auto player_pos = _players.find(player);
			auto s_pos = _globals.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = _globals.find(G_ID_ACTIVITY_END_TIME);
			if (player_pos == _players.end()) {
				_players.emplace(_self, [&](auto& info) {
					info.player = player;
					info.play_times = 1;
					info.in     = bet;
					info.out    = asset(0, bet.symbol);
					info.last_start_time = ct;
					info.last_close_time = 0;

					if (ct >= s_pos->val && ct <= e_pos->val) {
						info.activity_in = bet;
					}
				});
			} else {
				_players.modify(player_pos, 0, [&](auto& info) {
					info.play_times += 1;
					info.in         += bet;

					if (info.last_start_time < s_pos->val) {
						info.activity_in = asset(0, bet.symbol);
						info.activity_out = asset(0, bet.symbol);
					}
					info.last_start_time = ct;

					if (info.last_start_time >= s_pos->val && info.last_start_time <= e_pos->val) {
						info.activity_in += bet;
					}
					
				});
			}
		}
	}

	void blackjack::setglobal(uint64_t id, uint64_t val) {
        require_auth(_self);

        auto pos = _globals.find(id);
        if (pos == _globals.end()) {
            _globals.emplace(_self, [&](auto& info) {
                info.id = id;
                info.val = val;
            });
        } else {
            _globals.modify(pos, 0, [&](auto& info) {
                info.val = val;
            });
        }
	}

    void blackjack::settoken(asset risk, account_name token_account, asset min, bool divide_open, asset divide, uint32_t divide_time) {
        require_auth(_self);

        auto pos = _tradetokens.find(risk.symbol.name());
        if (pos == _tradetokens.end()) {
            if (token_account == 0) {
                return;
            }
            _tradetokens.emplace(_self, [&](auto& info) {
                info.risk_line = risk;
                info.token_account = token_account;
                info.min = min;
                info.in  = asset(0, risk.symbol);
                info.out = asset(0, risk.symbol);
				info.divide_open = false;
				info.divide_balance = divide;
				info.divide_time = divide_time*1e6;
                info.play_times = 0;
            });
        } else {
            if (token_account == 0) {
                _tradetokens.erase(pos);
            } else {
                _tradetokens.modify(pos, 0, [&](auto& info) {
                    info.risk_line = risk;
                    info.token_account = token_account;
                    info.min = min;
					info.divide_open = divide_open;
					info.divide_balance = divide;
					info.divide_time = divide_time*1e6;
                });
            }
        }
    }

	void blackjack::dealing(string action, uint64_t game_id) {
		require_auth(_self);

		auto idx = _games.get_index<N(byid)>();
		auto gm_pos = idx.find(game_id);
		eosio_assert(gm_pos != idx.end() && gm_pos->game_id == game_id, "dealing: game id does't exist!");

		transaction trx;
		trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealed), make_tuple(action, game_id));
		trx.delay_sec = 1;
		trx.send(gm_pos->player, _self);
	}

	void blackjack::dealed(string action, uint64_t game_id) {
		require_auth(_self);

		auto idx = _games.get_index<N(byid)>();
		auto gm_pos = idx.find(game_id);
		eosio_assert(gm_pos != idx.end() && gm_pos->game_id == game_id, "dealed: game id does't exist!");
		auto gm = *gm_pos;

		if (action == "new") {
			gm.deal_num += 1;
			gm.banker_cards.push_back(CARD_UNKOWN);
			gm.banker_cards.push_back(random_card(gm));
			vector<uint8_t> temp = gm.banker_cards;
			replace_banker_first_card(gm);

			gm.player_cards.push_back(random_card(gm));
			gm.player_cards.push_back(random_card(gm));

			uint8_t banker_points = cal_points(gm.banker_cards);
			uint8_t player_points = cal_points(gm.player_cards);
			eosio_assert(banker_points <= GAME_MAX_POINTS && player_points <= GAME_MAX_POINTS, "it's impossible that points larger than 21 in first deal! ");
			if (player_points == GAME_MAX_POINTS) {
				gm.status = GAME_STATUS_STANDED;
			} else {
				auto banker_open_card = gm.banker_cards[1];
				if (!is_A(banker_open_card) && banker_points == GAME_MAX_POINTS) {
					gm.status = GAME_STATUS_STANDED;
				} else {
					gm.banker_cards = temp;
				}
			}
		} else if (action == "hit") {
			gm.deal_num += 1;
			gm.player_cards.push_back(random_card(gm));
			uint8_t player_points = cal_points(gm.player_cards);
			if (player_points == GAME_MAX_POINTS) {
				gm.status = GAME_STATUS_STANDED;
				replace_banker_first_card(gm);
				deal_to_banker(gm);

			} else if (player_points > GAME_MAX_POINTS) {
				gm.status = GAME_STATUS_STANDED;
				replace_banker_first_card(gm);
			}
		} else if (action == "double") {
			gm.deal_num += 1;
			gm.double_down = true;
			gm.player_cards.push_back(random_card(gm));

			uint8_t player_points = cal_points(gm.player_cards);

			gm.status = GAME_STATUS_STANDED;
			if (player_points <= GAME_MAX_POINTS) {
				replace_banker_first_card(gm);
				deal_to_banker(gm);
			} else {
				replace_banker_first_card(gm);
			}
		} else if (action == "stand") {
			gm.manual_stand = true;
			gm.status = GAME_STATUS_STANDED;
			replace_banker_first_card(gm);
			deal_to_banker(gm);
		} else if (action == "surrender") {
			replace_banker_first_card(gm);
			gm.surrender = true;
			gm.status = GAME_STATUS_STANDED;
		} else if (action == "insure" || action == "uninsure") {
			eosio_assert(gm.deal_num == 1, "can insure/uninsure only after first deal!");
			gm.insure_status = 1;
			vector<uint8_t> temp = gm.banker_cards;
			replace_banker_first_card(gm);
			auto banker_points = cal_points(gm.banker_cards);
			if (banker_points == GAME_MAX_POINTS) {
				gm.status = GAME_STATUS_STANDED;
			} else {
				gm.banker_cards = temp;
			}
		} else {
			eosio_assert(false, "dealed: unknown action");
		}

		idx.modify(gm_pos, 0, [&](auto& info) {
			info = gm;
		});

		if (gm.status == GAME_STATUS_STANDED) {
			transaction trx;
			trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(close), gm.game_id);
			trx.delay_sec = 0;
			trx.send(gm.player, _self);
		}
	}

	void blackjack::stand(account_name player) {
		require_auth(player);

		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "you have no game in progress!");

		assert_not_operation_if_need_insure(*pos);

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("stand"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);
	}

	void blackjack::insure(account_name player, const asset& in) {
	
		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "insure: you have no game in progress!");
		eosio_assert((pos->bet / 2) == in, "insure: error quantity!");
		eosio_assert(pos->deal_num == 1, "insure: can insure only after first deal!");
		eosio_assert(pos->banker_cards.size() == 2 && is_A(pos->banker_cards[1]), "insure: banker's second card is not A!");
		eosio_assert(pos->insure_status == 0, "insure: duplicated");

		_games.modify(pos, 0, [&](auto& info) {
			info.insure = in;
		});

		if (pos->banker == _self) {
			auto token_pos = _tradetokens.find(in.symbol.name());
			_tradetokens.modify(token_pos, 0, [&](auto& info) {
				info.in += in;
			});
		} else {

		}

		if (in.symbol == EOS_SYMBOL) {
			auto player_pos = _players.find(player);
			auto s_pos = _globals.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = _globals.find(G_ID_ACTIVITY_END_TIME);
			_players.modify(player_pos, 0, [&](auto& info) {
				info.in         += in;

				if (info.last_start_time >= s_pos->val && info.last_start_time <= e_pos->val) {
					info.activity_in += in;
				}
			});
		}

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("insure"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);
	}

	void blackjack::hit(account_name player) {
		require_auth(player);

		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "you have no game in progress!");

		assert_not_operation_if_need_insure(*pos);

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("hit"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);
	}

	void blackjack::uninsure(account_name player) {
		require_auth(player);

		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "uninsure: you have no game in progress!");
		eosio_assert(pos->deal_num == 1, "uninsure: can uninsure only after first deal!");
		eosio_assert(pos->banker_cards.size() == 2 && is_A(pos->banker_cards[1]), "uninsure: banker's second card is not A!");
		eosio_assert(pos->insure_status == 0, "uninsure: duplicated");

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("uninsure"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);
	}

	void blackjack::double_down(account_name player, const asset& in) {

		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "double down: you have no game in progress!");

		eosio_assert(pos->bet == in, "double down: a error quantity!");
		eosio_assert(pos->deal_num == 1, "double down: only for first two cards");

		assert_not_operation_if_need_insure(*pos);

		auto player_points = cal_points(pos->player_cards);
		eosio_assert(player_points == 11, "double down: error points, only when points is 11 !");

		_games.modify(pos, 0, [&](auto& info) {
			info.bet += in;
		});

		if (pos->banker == _self) {
			auto token_pos = _tradetokens.find(in.symbol.name());
			_tradetokens.modify(token_pos, 0, [&](auto& info) {
				info.in += in;
			});
		} else {

		}

		if (in.symbol == EOS_SYMBOL) {
			auto player_pos = _players.find(player);
			auto s_pos = _globals.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = _globals.find(G_ID_ACTIVITY_END_TIME);
			_players.modify(player_pos, 0, [&](auto& info) {
				info.in         += in;

				if (info.last_start_time >= s_pos->val && info.last_start_time <= e_pos->val) {
					info.activity_in += in;
				}
			});
		}

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("double"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);
	}

    void blackjack::surrender(account_name player) {
		require_auth(player);

		eosio_assert(false, "function isn't opened!");
		auto pos = _games.find(player);
		eosio_assert(pos != _games.end() && pos->status == GAME_STATUS_BEGAN , "you have no game in progress!");

		assert_not_operation_if_need_insure(*pos);

		transaction deal_trx;
		deal_trx.actions.emplace_back(permission_level{ _self, N(active) }, _self, N(dealing), make_tuple(string("surrender"), pos->game_id));
		deal_trx.delay_sec = 0;
		deal_trx.send(pos->player, _self);

    }

	void blackjack::close(uint64_t game_id) {
		require_auth(_self);
		
		auto ct = current_time();
		auto idx = _games.get_index<N(byid)>();
		auto gm_pos = idx.find(game_id);
		eosio_assert(gm_pos != idx.end() && gm_pos->game_id == game_id, "game id doesn't exist!");
		eosio_assert(gm_pos->status == GAME_STATUS_STANDED, "game is not standed!");
		auto gm = *gm_pos;

		if (gm.surrender == true) {
			gm.payout.push_back(gm.bet / 2);
			gm.result = "surrender";
		} else {
			auto banker_points = cal_points(gm.banker_cards);
			auto player_points = cal_points(gm.player_cards);

			if (player_points == GAME_MAX_POINTS) {
				if (banker_points == GAME_MAX_POINTS) {
					gm.payout.push_back(gm.bet);
					gm.result = "push";
				} else {
					if (gm.deal_num == 1) {
						gm.payout.push_back(gm.bet * 25 / 10);
					} else {
						gm.payout.push_back(gm.bet * 2);	
					}
					gm.result = "win";
				}
				
			} else if (player_points > GAME_MAX_POINTS) {
				gm.payout.push_back(asset(0, gm.bet.symbol));
				gm.result = "lose";
			} else {
				if (banker_points == GAME_MAX_POINTS) {
					if (gm.deal_num == 1 && gm.insure.amount > 0 && gm.manual_stand == false) {
						gm.payout.push_back(gm.bet);
					} else {
						gm.payout.push_back(asset(0, gm.bet.symbol));
					}
					gm.result = "lose";
				} else if (banker_points > GAME_MAX_POINTS) {
					gm.payout.push_back(gm.bet * 2);
					gm.result = "win";
				} else {
					if (player_points == banker_points) {
						gm.payout.push_back(gm.bet);
						gm.result = "push";
					} else if (player_points > banker_points) {
						gm.payout.push_back(gm.bet * 2);
						gm.result = "win";
					} else {
						gm.payout.push_back(asset(0, gm.bet.symbol));
						gm.result = "lose";
					}
				}
			}
		}

		gm.status = GAME_STATUS_CLOSED;
		gm.close_time = ct;

		idx.modify(gm_pos, 0, [&](auto& info) {
			info = gm;
		});

		auto token_pos = _tradetokens.find(gm.bet.symbol.name());
		auto payout = asset_from_vec(gm.payout, gm.bet.symbol);
		if (gm.banker == _self) {
			_tradetokens.modify(token_pos, 0, [&](auto& info) {
				info.out += payout;
			});
		} else {

		}

		if (gm.bet.symbol == EOS_SYMBOL) {
			auto player_pos = _players.find(gm.player);
			auto s_pos = _globals.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = _globals.find(G_ID_ACTIVITY_END_TIME);
			_players.modify(player_pos, 0, [&](auto& info) {
				info.out        += payout;

				if (info.last_start_time >= s_pos->val && info.last_start_time <= e_pos->val) {
					info.activity_out += payout;
				}

				info.last_close_time = ct;
			});

			if (gm.banker == _self) {
				reward_game_token(gm);
			}
		}

		if (gm.result == "win") {
			if (gm.double_down == false) {
				auto win_pos = _globals.find(G_ID_COMMON_WIN_COUNT);
				_globals.modify(win_pos, 0, [&](auto& info) {
					info.val += 1;
				});
			} else {
				auto win_pos = _globals.find(G_ID_DOUBLE_WIN_COUNT);
				_globals.modify(win_pos, 0, [&](auto& info) {
					info.val += 1;
				});
			}
		} else if (gm.result == "lose") {
			auto win_pos = _globals.find(G_ID_LOSE_COUNT);
			_globals.modify(win_pos, 0, [&](auto& info) {
				info.val += 1;
			});
		} else if (gm.result == "push") {
			auto win_pos = _globals.find(G_ID_PUSH_COUNT);
			_globals.modify(win_pos, 0, [&](auto& info) {
				info.val += 1;
			});
		}

		account_name from = gm.banker;
		char msg[128];
		sprintf(msg, "[eos.win][banker: %s] Game result: %s! [https://eos.win/blackjack]", name{gm.banker}.to_string().c_str(), gm.result.c_str());
		for (size_t i=0; i<gm.payout.size(); i++) {
			asset out = gm.payout[i];
			if (out.amount > 0) {
				account_name token_account = token_account_for_symbol(out.symbol);
				INLINE_ACTION_SENDER(eosio::token, transfer)(token_account, {_self,N(active)}, {from, gm.player, out, msg} );
			}
		}

		SEND_INLINE_ACTION( *this, receipt, {_self,N(active)}, make_tuple(gm, cards_to_string(gm.banker_cards), cards_to_string(gm.player_cards), "normal close") );

		auto his_pos = _globals.find(G_ID_HISTORY_SIZE);
		if (his_pos->val >= GAME_MAX_HISTORY_SIZE) {
			_history.erase(_history.begin());
		}
		_history.emplace(_self, [&](auto& info) {
			info = gm;
		});
		_globals.modify(his_pos, 0, [&](auto& info) {
			info.val += 1;
		});
		
	}

	void blackjack::softclose(uint64_t game_id, string reason) {
		require_auth(_self);

		eosio_assert(reason.size() <= 256, "reason size must <= 256");
		
		auto idx = _games.get_index<N(byid)>();
		auto gm_pos = idx.find(game_id);
		eosio_assert(gm_pos != idx.end() && gm_pos->game_id == game_id, "game id doesn't exist!");
		eosio_assert(gm_pos->status == GAME_STATUS_BEGAN, "only for GAME_STATUS_BEGAN");

		idx.modify(gm_pos, 0, [&](auto& info) {
			info.status = GAME_STATUS_CLOSED;
		});
		
		auto token_pos = _tradetokens.find(gm_pos->bet.symbol.name());
		asset player_in = gm_pos->bet + gm_pos->insure;
		if (gm_pos->banker == _self) {
			_tradetokens.modify(token_pos, 0, [&](auto& info) {
				info.in -= player_in;
			});
		} else {

		}

		if (gm_pos->bet.symbol == EOS_SYMBOL) {
			auto player_pos = _players.find(gm_pos->player);
			auto s_pos = _globals.find(G_ID_ACTIVITY_START_TIME);
            auto e_pos = _globals.find(G_ID_ACTIVITY_END_TIME);
			_players.modify(player_pos, 0, [&](auto& info) {
				info.in         -= player_in;

				if (info.last_start_time >= s_pos->val && info.last_start_time <= e_pos->val) {
					info.activity_in -= player_in;
				}
			});
		}

		account_name from = gm_pos->banker;	
		char msg[128];
		sprintf(msg, "[eos.win][banker: %s] Game is forced to close, reason: %s. Your bet is returned! [https://eos.win/blackjack]", name{gm_pos->banker}.to_string().c_str(), reason.c_str());
		INLINE_ACTION_SENDER(eosio::token, transfer)(token_pos->token_account, {_self,N(active)}, {from, gm_pos->player, player_in, msg} );

		auto gm = *gm_pos;
		SEND_INLINE_ACTION( *this, receipt, {_self,N(active)}, make_tuple(gm, cards_to_string(gm.banker_cards), cards_to_string(gm.player_cards), "force close") );

	}

	void blackjack::hardclose(uint64_t game_id, string reason) {
		require_auth(_self);

		eosio_assert(reason.size() <= 256, "reason size must <= 256");

		auto idx = _games.get_index<N(byid)>();
		auto gm_pos = idx.find(game_id);

		eosio_assert(gm_pos != idx.end() && gm_pos->game_id == game_id, "game id doesn't exist!");

		idx.modify(gm_pos, 0, [&](auto& info) {
			info.status = GAME_STATUS_CLOSED;
		});
	}

    void blackjack::cleargames(uint32_t num) {
		require_auth(_self);
		auto idx = _games.get_index<N(bytime)>();
		auto ct = current_time();
		uint32_t count = 0;
		for (auto itr=idx.begin(); itr!=idx.end() && count < num; ) {
			if (itr->start_time + GAME_MAX_TIME <= ct) {
				if (itr->status == GAME_STATUS_CLOSED) {
					itr = idx.erase(itr);
				} else if (itr->status == GAME_STATUS_STANDED) {
					close(itr->game_id);
					itr = idx.erase(itr);
				} else if (itr->status == GAME_STATUS_BEGAN) {
					softclose(itr->game_id, "game time out, official release RAM");
					itr = idx.erase(itr);
				}
			}
			count += 1;
		}
	}

	void blackjack::receipt(game_item gm, string banker_cards, string player_cards, string memo) {
		require_auth(_self);

		require_recipient(gm.player);
	}
};

#define eosio_ABI_EX(TYPE, MEMBERS)                                            		\
extern "C" {                                                                   		\
    void apply(uint64_t receiver, uint64_t code, uint64_t action)              		\
    {                                                                         		\
        if (action == N(onerror))                                                	\
        {                                                                        	\
            /* onerror is only valid if it is for the "eosio" code account and     	\
            * authorized by "EOS"'s "active permission */                         	\
            eosio_assert(code == N(eosio), "onerror action's are only valid from "	\
            "the \"EOS\" system account");         									\
        }                                                                      		\
        auto self = receiver;                                                   	\
        if (code == N(eosio.token) && action == N(transfer))                     	\
        {                                                                       	\
            TYPE thiscontract(self);                                             	\
            eosio::execute_action(&thiscontract, &eoswin::blackjack::transfer);     \
        }                                                                       	\
        																			\
        if (code == self || action == N(onerror))                                	\
        {                                                                        	\
            TYPE thiscontract(self);                                                \
            switch (action)                                                         \
            {                                                                       \
                EOSIO_API(TYPE, MEMBERS)                                            \
            }                                                                      	\
        /* does not allow destructor of thiscontract to run: enu_exit(0); */   		\
        }                                                                        	\
    }                                                                          		\
}

eosio_ABI_EX(eoswin::blackjack, (init)(setglobal)(settoken)(dealing)(dealed)(stand)(hit)(uninsure)(surrender)(close)(softclose)(hardclose)(cleargames)(receipt))
