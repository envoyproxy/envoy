#include "murmur.h"
#define DT_CXX_FALLTHROUGH [[fallthrough]]

uint64_t murmurHash264(void const* key, size_t len, uint64_t seed) {
	static uint64_t const m = UINT64_C(0xc6a4a7935bd1e995);
	static int const r = 47;

	uint64_t h = seed ^ (len * m);

	uint64_t const* data = reinterpret_cast<uint64_t const*>(key);
	uint64_t const* end = data + (len / 8);

	while (data != end) {
		uint64_t k = *data;
        data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	unsigned char const* data2 = reinterpret_cast<unsigned char const*>(data);

	switch (len & 7) {
	case 7:
		h ^= static_cast<uint64_t>(data2[6]) << 48;
		DT_CXX_FALLTHROUGH; /* no break */
	case 6:
		h ^= static_cast<uint64_t>(data2[5]) << 40;
		DT_CXX_FALLTHROUGH; /* no break */
	case 5:
		h ^= static_cast<uint64_t>(data2[4]) << 32;
		DT_CXX_FALLTHROUGH; /* no break */
	case 4:
		h ^= static_cast<uint64_t>(data2[3]) << 24;
		DT_CXX_FALLTHROUGH; /* no break */
	case 3:
		h ^= static_cast<uint64_t>(data2[2]) << 16;
		DT_CXX_FALLTHROUGH; /* no break */
	case 2:
		h ^= static_cast<uint64_t>(data2[1]) << 8;
		DT_CXX_FALLTHROUGH; /* no break */
	case 1:
		h ^= static_cast<uint64_t>(data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}