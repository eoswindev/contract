#include <vector>
#include <string>
#include <eosiolib/eosio.hpp>
#include <eosiolib/types.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/symbol.hpp>
#include <eosiolib/currency.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/transaction.hpp>
#include <boost/algorithm/string.hpp>
#include <eosiolib/crypto.h>


using namespace std;

#define GLOBAL_ID_CUR_ROUND_ID 1
#define GLOBAL_ID_AMOUNT_PER_NUM 2
#define GLOBAL_ID_MAX_AMOUNT_PER_ROUND 3
#define AUTH_ACCOUNT N(eosluckydice)

class jackpot : public eosio::contract 
{
public:
	const uint64_t max_number_count = 200;

	struct st_seeds {
    	checksum256 seed1;
    	checksum256 seed2;
    };

	//@abi table players
	struct player
	{
		account_name name;
		uint64_t total_num;
		uint64_t start;
		uint64_t end;
		eosio::time_point_sec time;
		uint64_t primary_key() const { return name; };
    	EOSLIB_SERIALIZE(player, (name)(total_num)(start)(end)(time));
	};
	typedef eosio::multi_index<N(players), player> player_index;
	player_index _players;

	//@abi table rounds
	struct round
	{
		uint64_t id;
		uint64_t total_num;
		account_name winner;
		uint64_t winner_num;
		uint64_t winner_score;
		eosio::asset profit;
		eosio::time_point_sec start_time;
		eosio::time_point_sec end_time;
		uint64_t primary_key() const { return id; };
		EOSLIB_SERIALIZE(round, (id)(total_num)(winner)(winner_num)(winner_score)(profit)(start_time)(end_time));
	};
	typedef eosio::multi_index<N(rounds), round> round_index;
	round_index _rounds;
	round _cur_round;

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

	//@abi table numbers
	struct number
	{
		uint64_t id;
		account_name owner;
		uint64_t primary_key() const { return id; };
    	EOSLIB_SERIALIZE(number, (id)(owner));
	};
	typedef eosio::multi_index<N(numbers), number> number_index;
    number_index _numbers;

	jackpot(account_name self) : contract(self),
							   	_players(self, self),
								_globals(self, self),
								_numbers(self, self),
								_rounds(self, self)
	{
		auto cur_round_iter = _globals.find(GLOBAL_ID_CUR_ROUND_ID);
		
		uint64_t cur_round_id;
		if (cur_round_iter != _globals.end()) {
			cur_round_id = cur_round_iter->val;
			_cur_round = _rounds.get(cur_round_id, "construct get cur");
		}
	}

	~jackpot()
	{
		auto round_iter = _rounds.find(_cur_round.id);
		if (round_iter != _rounds.end())
		{
			_rounds.modify(round_iter, 0, [&](auto& a) {
				a.total_num = _cur_round.total_num;
				a.winner = _cur_round.winner;
				a.winner_num = _cur_round.winner_num;
				a.winner_score = _cur_round.winner_score;
				a.profit = _cur_round.profit;
				a.end_time = _cur_round.end_time;
			});
		}
	}

	/// @abi action
	void init()
	{
		require_auth(AUTH_ACCOUNT);
		_rounds.emplace(_self, [&](auto& a) {
			a.id = 1;
			a.total_num = 0;
			a.start_time = eosio::time_point_sec( now() );
		});

		_globals.emplace(_self, [&](auto& a) {
			a.id = GLOBAL_ID_CUR_ROUND_ID;
			a.val = 1;
		});
	}
	
	void transfer(account_name from, account_name to, eosio::asset quantity, string memo)
    {
    	eosio::currency::transfer t = {from, to, quantity, memo};
    	if (from == _self || to != _self || from != AUTH_ACCOUNT)
    	{
    		return;
    	}

		vector<string> pieces;
        boost::split(pieces, memo, boost::is_any_of("-"));

		account_name account = eosio::string_to_name(pieces[0].c_str());
		
		auto amount_iter = _globals.find(GLOBAL_ID_AMOUNT_PER_NUM);
		uint64_t amount_per_num = 0;
		if (amount_iter != _globals.end()) {
			amount_per_num = amount_iter->val;
		}
		uint64_t total_num = quantity.amount * 10 / amount_per_num;
		if (quantity.amount == 12) {
			total_num = 1;
		}
		
		if (total_num >= max_number_count) {
			total_num = max_number_count;
		}
		auto player_iter = _players.find(account);
		if (player_iter == _players.end()) {
			_players.emplace(_self, [&](auto &a) {
				a.name = account;
				a.total_num = total_num;
				a.time = eosio::time_point_sec( now() );
			});
		} else {
			if (player_iter->time <= _cur_round.start_time) {
				_players.modify(player_iter, 0, [&](auto& a) {
					a.total_num = 0;
					a.start = 0;
					a.end = 0;
				});
			}

			_players.modify(player_iter, 0, [&](auto& a) {
				a.total_num = a.total_num + total_num;
				a.time = eosio::time_point_sec( now() );
			});
		}

		uint64_t start = _cur_round.total_num;
		uint64_t end = start + total_num - 1;

		for (uint64_t i = start; i <= end; i++) {
			auto number_iter = _numbers.find(i);
			if (number_iter != _numbers.end()) {
				_numbers.modify(number_iter, 0, [&](auto& a) {
					a.owner = account;
				});
			} else {
				_numbers.emplace(_self, [&](auto& a) {
					a.id = i;
					a.owner = account;
				});
			}
		}

		_cur_round.total_num += total_num;
		_cur_round.profit += quantity;

		auto max_iter = _globals.find(GLOBAL_ID_MAX_AMOUNT_PER_ROUND);
		
		uint64_t max_profit = max_iter->val * amount_per_num / 10;
		if (_cur_round.profit.amount >= max_profit) {
			draw();
		}
    }
    
	/// @abi action
	void draw()
	{
		require_auth(AUTH_ACCOUNT);
		
		uint64_t total_number = _cur_round.total_num;

		auto s = read_transaction(nullptr, 0);
		char *tx = (char *)malloc(s);
		read_transaction(tx, s);
		checksum256 tx_hash;
		sha256(tx, s, &tx_hash);

		auto global_iter = _globals.find(GLOBAL_ID_CUR_ROUND_ID);
		checksum256 system_seed;
		sha256( (char *)&global_iter, sizeof(global_iter), &system_seed);

		st_seeds seeds;
		seeds.seed1 = system_seed;
		seeds.seed2 = tx_hash;

		checksum256 seed_hash;
		sha256( (char *)&seeds.seed1, sizeof(seeds.seed1) * 2, &seed_hash);

		const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&seed_hash);
		uint64_t hash_added = p64[1];
		uint64_t random_roll = (hash_added % total_number);
		
		auto number_iter = _numbers.find(random_roll);
		_cur_round.winner = number_iter->owner;
		_cur_round.winner_num = random_roll;

		auto player_iter = _players.find(_cur_round.winner);
		if (player_iter != _players.end()) {
			_cur_round.winner_score = player_iter->total_num;
		}

		uint64_t _now = now();
		_cur_round.end_time = eosio::time_point_sec( _now );
		
		eosio::action(
			eosio::permission_level{_self, N(active)},
			N(eosio.token), 
			N(transfer),
			std::make_tuple(
				_self, 
				_cur_round.winner,
				_cur_round.profit, 
				std::string("Jackpot round ") + std::to_string(_cur_round.id) + std::string(" bonus")
			)
		).send();
		
		newround();
	}

	void newround()
	{
		require_auth(AUTH_ACCOUNT);

		_rounds.emplace(_self, [&](auto& a) {
			a.id = _cur_round.id + 1;
			a.total_num = 0;
			a.start_time = eosio::time_point_sec( now() );
		});

		auto global_round_iter = _globals.find(GLOBAL_ID_CUR_ROUND_ID);
		_globals.modify(global_round_iter, 0, [&](auto& a) {
			a.val = _cur_round.id + 1;
		});
	}

	/// @abi action
    void setglobal(uint64_t id, uint64_t value)
    {
      require_auth(AUTH_ACCOUNT);
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
};

#define EOSIO_ABI_EX(TYPE, MEMBERS)												\
	extern "C"																	\
	{																			\
		void apply(uint64_t receiver, uint64_t code, uint64_t action) {			\
			if (action == N(onerror)) {											\
				return;															\
			}																	\
			TYPE thiscontract(receiver);										\
			if (code == N(eosio.token) && action == N(transfer)) {				\
				execute_action(&thiscontract, &jackpot::transfer);				\
			}																	\
			if (code != receiver) return;										\
			switch (action) {													\
				EOSIO_API(TYPE, MEMBERS);										\
			}																	\
		}																		\
	}

EOSIO_ABI_EX(jackpot, (init)(draw)(setglobal))
