// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <random>

#include "envoy/http/header_map.h"

#include "source/common/common/trie_lookup_table.h"
#include "source/common/http/headers.h"

#include "benchmark/benchmark.h"

namespace Envoy {

// NOLINT(namespace-envoy)

template <class TableType>
static void typedBmTrieLookups(benchmark::State& state, std::vector<std::string>& keys) {
  std::mt19937 prng(1); // PRNG with a fixed seed, for repeatability
  std::uniform_int_distribution<size_t> keyindex_distribution(0, keys.size() - 1);
  TableType trie;
  for (const std::string& key : keys) {
    trie.add(key, nullptr);
  }
  std::vector<size_t> key_selections;
  for (size_t i = 0; i < 1024; i++) {
    key_selections.push_back(keyindex_distribution(prng));
  }

  // key_index indexes into key_selections which is a pre-selected
  // random ordering of 1024 indexes into the existing keys. This
  // way we read from all over the trie, without spending time during
  // the performance test generating these random choices.
  size_t key_index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto v = trie.find(keys[key_selections[key_index++]]);
    // Reset key_index to 0 whenever it reaches 1024.
    key_index &= 1023;
    benchmark::DoNotOptimize(v);
  }
}

// Range args are:
// 0 - num_keys
// 1 - key_length (0 is a special case that generates mixed-length keys)
template <class TableType> static void typedBmTrieLookups(benchmark::State& state) {
  std::mt19937 prng(1); // PRNG with a fixed seed, for repeatability
  int num_keys = state.range(0);
  int key_length = state.range(1);
  std::uniform_int_distribution<short> char_distribution('a', 'z');
  std::uniform_int_distribution<size_t> key_length_distribution(key_length == 0 ? 8 : key_length,
                                                                key_length == 0 ? 128 : key_length);
  auto make_key = [&](size_t len) {
    std::string ret;
    for (size_t i = 0; i < len; i++) {
      ret.push_back(static_cast<char>(char_distribution(prng)));
    }
    return ret;
  };
  std::vector<std::string> keys;
  for (int i = 0; i < num_keys; i++) {
    std::string key = make_key(key_length_distribution(prng));
    keys.push_back(std::move(key));
  }
  typedBmTrieLookups<TableType>(state, keys);
}

static void bmTrieLookups(benchmark::State& s) {
  typedBmTrieLookups<TrieLookupTable<const void*>>(s);
}

#define ADD_HEADER_TO_KEYS(name) keys.emplace_back(Http::Headers::get().name);
static void bmTrieLookupsRequestHeaders(benchmark::State& s) {
  std::vector<std::string> keys;
  INLINE_REQ_HEADERS(ADD_HEADER_TO_KEYS);
  typedBmTrieLookups<TrieLookupTable<const void*>>(s, keys);
}
static void bmTrieLookupsResponseHeaders(benchmark::State& s) {
  std::vector<std::string> keys;
  INLINE_RESP_HEADERS(ADD_HEADER_TO_KEYS);
  typedBmTrieLookups<TrieLookupTable<const void*>>(s, keys);
}

BENCHMARK(bmTrieLookupsRequestHeaders);
BENCHMARK(bmTrieLookupsResponseHeaders);
BENCHMARK(bmTrieLookups)->ArgsProduct({{10, 100, 1000, 10000}, {0, 8, 128}});

} // namespace Envoy
