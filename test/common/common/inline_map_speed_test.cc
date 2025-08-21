#include <cstddef>

#include "source/common/common/inline_map.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapFind(benchmark::State& state) {
  size_t map_type = state.range(0);
  size_t used_inline_handles = state.range(1);

  std::vector<std::string> normal_keys;

  InlineMapDescriptor<std::string> descriptor_200;
  // Create 200 inline keys.
  std::vector<InlineMapDescriptor<std::string>::Handle> inline_handles_200;
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    inline_handles_200.push_back(descriptor_200.addInlineKey(key));
    normal_keys.push_back(key);
  }
  InlineMapDescriptor<std::string> descriptor_0;

  descriptor_200.finalize();
  descriptor_0.finalize();

  absl::flat_hash_map<std::string, std::string> normal_map;
  auto inline_map_200 = InlineMap<std::string, std::string>::create(descriptor_200);
  auto inline_map_0 = InlineMap<std::string, std::string>::create(descriptor_0);

  for (size_t i = 0; i < 200; ++i) {
    normal_map[normal_keys[i]] = "value_" + std::to_string(i);
    inline_map_200->set(normal_keys[i], "value_" + std::to_string(i));
    inline_map_0->set(normal_keys[i], "value_" + std::to_string(i));
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
        inline_map_0->get(normal_keys[i]);
      }
    }
  } else {
    // Inline map with 200 inline keys.
    if (used_inline_handles == 0) {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200->get(normal_keys[i]);
        }
      }
    } else {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200->get(inline_handles_200[i]);
        }
      }
    }
  }
}

// Additional `{0, 0}` is used to avoid the effect of the first run.
BENCHMARK(BM_InlineMapFind)->Args({0, 0})->Args({0, 0})->Args({1, 0})->Args({2, 0})->Args({2, 1});

} // namespace
} // namespace Envoy
