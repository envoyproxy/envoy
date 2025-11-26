// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <random>

#include "source/common/common/radix_tree.h"
#include "source/common/http/headers.h"

#include "benchmark/benchmark.h"

namespace Envoy {

// NOLINT(namespace-envoy)

#define ADD_HEADER_TO_KEYS(name) keys.emplace_back(Http::Headers::get().name);

// Helper function to generate test data with hierarchical prefixes
std::vector<std::string> generateHierarchicalKeys(int num_keys, int max_depth) {
  std::mt19937 prng(1); // PRNG with a fixed seed, for repeatability
  std::uniform_int_distribution<short> char_distribution('a', 'z');
  std::uniform_int_distribution<int> depth_distribution(1, max_depth);

  std::vector<std::string> keys;
  for (int i = 0; i < num_keys; i++) {
    int depth = depth_distribution(prng);
    std::string key;
    for (int j = 0; j < depth; j++) {
      for (int k = 0; k < 3; k++) { // Each level has 3 characters
        key.push_back(static_cast<char>(char_distribution(prng)));
      }
      if (j < depth - 1) {
        key.push_back('/'); // Use '/' as separator for hierarchical structure
      }
    }
    keys.push_back(key);
  }
  return keys;
}

// Helper function to generate search keys with various prefix lengths
std::vector<std::string> generateSearchKeys(const std::vector<std::string>& keys,
                                            int num_searches) {
  std::mt19937 prng(2); // Different seed for search keys
  std::uniform_int_distribution<size_t> keyindex_distribution(0, keys.size() - 1);
  std::uniform_int_distribution<size_t> length_distribution(1, 20); // Random prefix length

  std::vector<std::string> search_keys;
  for (int i = 0; i < num_searches; i++) {
    const std::string& base_key = keys[keyindex_distribution(prng)];
    size_t prefix_len = std::min(length_distribution(prng), base_key.length());
    search_keys.push_back(base_key.substr(0, prefix_len));
  }
  return search_keys;
}

template <class TableType>
static void typedBmPrefixMatching(benchmark::State& state, const std::vector<std::string>& keys,
                                  const std::vector<std::string>& search_keys) {
  TableType table;
  for (const std::string& key : keys) {
    table.add(key, nullptr);
  }

  size_t search_index = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto result = table.findMatchingPrefixes(search_keys[search_index++]);
    // Reset search_index to 0 whenever it reaches the end
    search_index %= search_keys.size();
    benchmark::DoNotOptimize(result);
  }
}

// Range args are:
// 0 - num_keys (number of keys in the tree)
// 1 - max_depth (maximum depth of hierarchical keys)
// 2 - num_searches (number of search operations to perform)
template <class TableType> static void typedBmPrefixMatching(benchmark::State& state) {
  int num_keys = state.range(0);
  int max_depth = state.range(1);
  int num_searches = state.range(2);

  std::vector<std::string> keys = generateHierarchicalKeys(num_keys, max_depth);
  std::vector<std::string> search_keys = generateSearchKeys(keys, num_searches);

  typedBmPrefixMatching<TableType>(state, keys, search_keys);
}

// Benchmark for RadixTree
static void bmRadixTreePrefixMatching(benchmark::State& s) {
  typedBmPrefixMatching<RadixTree<const void*>>(s);
}

static void bmRadixTreeRequestHeadersPrefixMatching(benchmark::State& s) {
  std::vector<std::string> keys;
  INLINE_REQ_HEADERS(ADD_HEADER_TO_KEYS);

  // Generate search keys based on the headers
  std::vector<std::string> search_keys = generateSearchKeys(keys, 1000);

  typedBmPrefixMatching<RadixTree<const void*>>(s, keys, search_keys);
}

static void bmRadixTreeResponseHeadersPrefixMatching(benchmark::State& s) {
  std::vector<std::string> keys;
  INLINE_RESP_HEADERS(ADD_HEADER_TO_KEYS);

  // Generate search keys based on the headers
  std::vector<std::string> search_keys = generateSearchKeys(keys, 1000);

  typedBmPrefixMatching<RadixTree<const void*>>(s, keys, search_keys);
}

BENCHMARK(bmRadixTreePrefixMatching)
    ->ArgsProduct({{100, 1000, 10000}, {3, 5, 8}, {1000}})
    ->Name("RadixTree/PrefixMatching");

BENCHMARK(bmRadixTreeRequestHeadersPrefixMatching)->Name("RadixTree/RequestHeadersPrefixMatching");

BENCHMARK(bmRadixTreeResponseHeadersPrefixMatching)
    ->Name("RadixTree/ResponseHeadersPrefixMatching");

} // namespace Envoy
