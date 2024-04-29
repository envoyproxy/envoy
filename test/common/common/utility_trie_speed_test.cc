// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <random>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace Envoy {

// NOLINT(namespace-envoy)

using KeyLengthRange = std::pair<size_t, size_t>;

template <template <class> class EntryType>
static void typedBmTrieLookups(benchmark::State& state, const size_t num_keys,
                               const KeyLengthRange key_length_range) {
  std::mt19937 prng(1); // PRNG with a fixed seed, for repeatability
  std::uniform_int_distribution<char> char_distribution('a', 'z');
  std::uniform_int_distribution<size_t> key_length_distribution(key_length_range.first,
                                                                key_length_range.second);
  std::uniform_int_distribution<size_t> keyindex_distribution(0, num_keys - 1);
  auto make_key = [&](size_t len) {
    std::string ret;
    for (size_t i = 0; i < len; i++) {
      ret.push_back(char_distribution(prng));
    }
    return ret;
  };
  TrieLookupTable<EntryType, const void*> trie;
  std::vector<std::string> keys;
  for (size_t i = 0; i < num_keys; i++) {
    std::string key = make_key(key_length_distribution(prng));
    trie.add(key, reinterpret_cast<const void*>(&num_keys));
    keys.push_back(std::move(key));
  }

  std::vector<size_t> key_selections;
  for (size_t i = 0; i < 1024; i++) {
    key_selections.push_back(keyindex_distribution(prng));
  }
  size_t key_index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto v = trie.find(keys[key_selections[key_index++]]);
    key_index &= 1023;
    benchmark::DoNotOptimize(v);
  }
}

// Args are:
// 0 - size_t num_keys
// 1 - std::pair<size_t, size_t> key_length_range
template <typename... Args> static void bmFastTrieLookups(benchmark::State& state, Args&&... args) {
  auto args_t = std::make_tuple(std::move(args)...);
  typedBmTrieLookups<FastTrieEntry>(state, std::get<0>(args_t), std::get<1>(args_t));
}
template <typename... Args>
static void bmSmallTrieLookups(benchmark::State& state, Args&&... args) {
  auto args_t = std::make_tuple(std::move(args)...);
  typedBmTrieLookups<SmallTrieEntry>(state, std::get<0>(args_t), std::get<1>(args_t));
}

BENCHMARK_CAPTURE(bmFastTrieLookups, 10_short_keys, 10, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10_short_keys, 10, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmFastTrieLookups, 10_mixed_keys, 10, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10_mixed_keys, 10, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmFastTrieLookups, 10_long_keys, 10, KeyLengthRange(128, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10_long_keys, 10, KeyLengthRange(128, 128));
BENCHMARK_CAPTURE(bmFastTrieLookups, 100_short_keys, 100, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 100_short_keys, 100, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmFastTrieLookups, 100_mixed_keys, 100, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 100_mixed_keys, 100, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmFastTrieLookups, 100_long_keys, 100, KeyLengthRange(128, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 100_long_keys, 100, KeyLengthRange(128, 128));
BENCHMARK_CAPTURE(bmFastTrieLookups, 10000_short_keys, 10000, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10000_short_keys, 10000, KeyLengthRange(8, 8));
BENCHMARK_CAPTURE(bmFastTrieLookups, 10000_mixed_keys, 10000, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10000_mixed_keys, 10000, KeyLengthRange(8, 128));
BENCHMARK_CAPTURE(bmFastTrieLookups, 10000_long_keys, 10000, KeyLengthRange(128, 128));
BENCHMARK_CAPTURE(bmSmallTrieLookups, 10000_long_keys, 10000, KeyLengthRange(128, 128));

} // namespace Envoy
