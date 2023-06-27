#include <cstddef>

#include "source/common/common/inline_map_registry.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapFind(benchmark::State& state) {
  size_t map_type = state.range(0);
  size_t used_inline_handles = state.range(1);

  std::vector<std::string> normal_keys;

  InlineMapRegistry<std::string> registry_200;
  // Create 200 inline keys.
  std::vector<InlineMapRegistry<std::string>::InlineKey> inline_handles_200;
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    inline_handles_200.push_back(registry_200.registerInlineKey(key));
    normal_keys.push_back(key);
  }
  InlineMapRegistry<std::string> registry_0;

  registry_200.finalize();
  registry_0.finalize();

  absl::flat_hash_map<std::string, std::string> normal_map;
  auto inline_map_200 = InlineMap<std::string, std::string>::create(registry_200);
  auto inline_map_0 = InlineMap<std::string, std::string>::create(registry_0);

  for (size_t i = 0; i < 200; ++i) {
    normal_map[normal_keys[i]] = "value_" + std::to_string(i);
    inline_map_200->insert(normal_keys[i], "value_" + std::to_string(i));
    inline_map_0->insert(normal_keys[i], "value_" + std::to_string(i));
  }

  if (map_type == 0) {
    // Normal map.
    for (auto _ : state) { // NOLINT
      for (size_t i = 0; i < 200; ++i) {
        normal_map.find(normal_keys[i]);
      }
    }
  } else if (map_type == 1) {
    // Inline map without any inline keys.
    for (auto _ : state) { // NOLINT
      for (size_t i = 0; i < 200; ++i) {
        inline_map_0->lookup(normal_keys[i]);
      }
    }
  } else {
    // Inline map with 200 inline keys.
    if (used_inline_handles == 0) {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200->lookup(normal_keys[i]);
        }
      }
    } else {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200->lookup(inline_handles_200[i]);
        }
      }
    }
  }
}

// Additional `{0, 0}` is used to avoid the effect of the first run.
BENCHMARK(BM_InlineMapFind)->Args({0, 0})->Args({0, 0})->Args({1, 0})->Args({2, 0})->Args({2, 1});

} // namespace
} // namespace Envoy
