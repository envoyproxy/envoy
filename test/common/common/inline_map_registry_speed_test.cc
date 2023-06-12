#include "envoy/common/inline_map_registry.h"

#include "benchmark/benchmark.h"
#include <cstddef>

namespace Envoy {
namespace {

class BenchmarkTestScope {
public:
  static absl::string_view name() { return "BenchmarkTestScope"; }
};

static std::vector<std::string> normal_keys = {};

static std::vector<InlineMapRegistry<BenchmarkTestScope>::InlineHandle> initializeRegistry() {
  std::vector<InlineMapRegistry<BenchmarkTestScope>::InlineHandle> handles;

  for (size_t i = 0; i < 200; ++i) {
    std::string key = "key_" + std::to_string(i);
    normal_keys.push_back(key);
    handles.push_back(InlineMapRegistry<BenchmarkTestScope>::registerInlineKey(key));
  }

  // Force the inline map registry to be finalized.
  InlineMapRegistryManager::registryInfos();

  return handles;
}

static std::vector<InlineMapRegistry<BenchmarkTestScope>::InlineHandle> inline_handles =
    initializeRegistry();

// NOLINTNEXTLINE(readability-identifier-naming)
static void InlineMapFind(benchmark::State& state) {
  size_t map_type = state.range(0);
  size_t used_inline_handles = state.range(1);

  absl::flat_hash_map<std::string, std::unique_ptr<std::string>> normal_map;
  auto inline_map =
      InlineMapRegistry<BenchmarkTestScope>::InlineMap<std::string>::createInlineMap();

  for (size_t i = 0; i < 200; ++i) {
    normal_map[normal_keys[i]] = std::make_unique<std::string>("value_" + std::to_string(i));
    inline_map->insert(normal_keys[i], std::make_unique<std::string>("value_" + std::to_string(i)));
  }

  if (map_type == 0) {
    for (auto _ : state) { // NOLINT
      for (size_t i = 0; i < 200; ++i) {
        normal_map.find(normal_keys[i]);
      }
    }
  } else {
    if (used_inline_handles == 0) {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map->lookup(normal_keys[i]);
        }
      }
    } else {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map->lookup(inline_handles[i]);
        }
      }
    }
  }
}

BENCHMARK(InlineMapFind)->Args({0, 0})->Args({1, 0})->Args({1, 1});

} // namespace
} // namespace Envoy
