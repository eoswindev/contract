/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.h>
#include <eosiolib/crypto.h>

namespace eoswin {
    using namespace eosio;
    
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

		struct mix_seeds {
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
		mix_seeds seeds;
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


