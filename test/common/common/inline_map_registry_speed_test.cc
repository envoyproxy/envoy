#include <cstddef>

#include "envoy/common/inline_map_registry.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

template <size_t N> class BenchmarkTestScope {
public:
  static absl::string_view name() {
    static const std::string name = "BenchmarkTestScope_KeySize_" + std::to_string(N);
    return name;
  }

  static std::vector<typename InlineMapRegistry<BenchmarkTestScope>::InlineHandle>
  initializeRegistry() {
    std::vector<typename InlineMapRegistry<BenchmarkTestScope>::InlineHandle> handles;

    // Force the inline map registry to be initialized and be added to the registry manager.
    InlineMapRegistry<BenchmarkTestScope>::scopeId();

    for (size_t i = 0; i < N; ++i) {
      std::string key = "key_" + std::to_string(i);
      handles.push_back(InlineMapRegistry<BenchmarkTestScope>::registerInlineKey(key));
    }

    // Force the inline map registry to be finalized.
    InlineMapRegistryManager::registryInfos();

    return handles;
  }

  static std::vector<typename InlineMapRegistry<BenchmarkTestScope>::InlineHandle> inlineHandles() {
    static const std::vector<typename InlineMapRegistry<BenchmarkTestScope>::InlineHandle>
        inline_handles = initializeRegistry();
    return inline_handles;
  }
};

template <size_t N> static std::vector<std::string> getNormalKeys() {
  static const std::vector<std::string> normal_keys = []() {
    std::vector<std::string> keys;
    for (size_t i = 0; i < N; ++i) {
      keys.push_back("key_" + std::to_string(i));
    }
    return keys;
  }();
  return normal_keys;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapFind(benchmark::State& state) {
  size_t map_type = state.range(0);
  size_t used_inline_handles = state.range(1);

  auto normal_keys = getNormalKeys<200>();
  auto inline_handles_200 = BenchmarkTestScope<200>::inlineHandles();
  auto inline_hanldes_0 = BenchmarkTestScope<0>::inlineHandles();

  absl::flat_hash_map<std::string, std::unique_ptr<std::string>> normal_map;
  auto inline_map_200 =
      InlineMapRegistry<BenchmarkTestScope<200>>::InlineMap<std::string>::createInlineMap();
  auto inline_map_0 =
      InlineMapRegistry<BenchmarkTestScope<0>>::InlineMap<std::string>::createInlineMap();

  for (size_t i = 0; i < 200; ++i) {
    normal_map[normal_keys[i]] = std::make_unique<std::string>("value_" + std::to_string(i));
    inline_map_200->insert(normal_keys[i],
                           std::make_unique<std::string>("value_" + std::to_string(i)));
    inline_map_0->insert(normal_keys[i],
                         std::make_unique<std::string>("value_" + std::to_string(i)));
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
