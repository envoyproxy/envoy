#include <cstddef>

#include "source/common/common/inline_map.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapConstructAndDestruct(benchmark::State& state) {
  const size_t map_type = state.range(0);

  InlineMapDescriptor<std::string> descriptor_200;
  // Create 200 inline keys.
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    descriptor_200.addInlineKey(key);
  }
  InlineMapDescriptor<std::string> descriptor_0;

  descriptor_200.finalize();
  descriptor_0.finalize();

  const auto create_normal_map = []() {
    absl::flat_hash_map<std::string, std::string> normal_map;
    return normal_map.size();
  };

  const auto create_inline_map_200 = [&descriptor_200]() {
    InlineMap<std::string, std::string> inline_map(descriptor_200);
    return inline_map.size();
  };

  const auto create_inline_map_0 = [&descriptor_0]() {
    InlineMap<std::string, std::string> inline_map(descriptor_0);
    return inline_map.size();
  };

  if (map_type == 0) {
    // Normal map.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_normal_map());
    }
  } else if (map_type == 1) {
    // Inline map without any inline keys.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_inline_map_0());
    }
  } else {
    // Inline map with 200 inline keys.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_inline_map_200());
    }
  }
}

BENCHMARK(BM_InlineMapConstructAndDestruct)->Args({0})->Args({1})->Args({2});

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapConstructAndDestructWithSingleEntry(benchmark::State& state) {
  const size_t map_type = state.range(0);

  InlineMapDescriptor<std::string> descriptor_200;
  // Create 200 inline keys.
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    descriptor_200.addInlineKey(key);
  }
  InlineMapDescriptor<std::string> descriptor_0;

  descriptor_200.finalize();
  descriptor_0.finalize();

  const auto create_normal_map = []() {
    absl::flat_hash_map<std::string, std::string> normal_map;
    normal_map.insert({"key_1", "value_1"});
    return normal_map.size();
  };

  const auto create_inline_map_200 = [&descriptor_200]() {
    InlineMap<std::string, std::string> inline_map(descriptor_200);
    inline_map.set("key_1", "value_1");
    return inline_map.size();
  };

  const auto create_inline_map_0 = [&descriptor_0]() {
    InlineMap<std::string, std::string> inline_map(descriptor_0);
    inline_map.set("key_1", "value_1");
    return inline_map.size();
  };

  if (map_type == 0) {
    // Normal map.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_normal_map());
    }
  } else if (map_type == 1) {
    // Inline map without any inline keys.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_inline_map_0());
    }
  } else {
    // Inline map with 200 inline keys.
    for (auto _ : state) { // NOLINT
      benchmark::DoNotOptimize(create_inline_map_200());
    }
  }
}

BENCHMARK(BM_InlineMapConstructAndDestructWithSingleEntry)->Args({0})->Args({1})->Args({2});

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapSet(benchmark::State& state) {
  const size_t map_type = state.range(0);
  const size_t used_inline_handles = state.range(1);

  std::vector<std::string> normal_keys;
  std::vector<std::string> normal_values;

  InlineMapDescriptor<std::string> descriptor_200;
  // Create 200 inline keys.
  std::vector<InlineMapDescriptor<std::string>::Handle> inline_handles_200;
  for (size_t i = 0; i < 200; ++i) {
    const std::string key = "key_" + std::to_string(i);
    inline_handles_200.push_back(descriptor_200.addInlineKey(key));
    normal_keys.push_back(key);
    normal_values.push_back("value_" + std::to_string(i));
  }
  InlineMapDescriptor<std::string> descriptor_0;

  descriptor_200.finalize();
  descriptor_0.finalize();

  absl::flat_hash_map<std::string, std::string> normal_map;
  InlineMap<std::string, std::string> inline_map_200(descriptor_200);
  InlineMap<std::string, std::string> inline_map_0(descriptor_0);

  if (map_type == 0) {
    // Normal map.
    for (auto _ : state) { // NOLINT
      for (size_t i = 0; i < 200; ++i) {
        normal_map.insert({normal_keys[i], normal_values[i]});
      }
      normal_map.clear();
    }
  } else if (map_type == 1) {
    // Inline map without any inline keys.
    for (auto _ : state) { // NOLINT
      for (size_t i = 0; i < 200; ++i) {
        inline_map_0.set(normal_keys[i], normal_values[i]);
      }
      inline_map_0.clear();
    }
  } else {
    // Inline map with 200 inline keys.
    if (used_inline_handles == 0) {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200.set(normal_keys[i], normal_values[i]);
        }
        inline_map_200.clear();
      }
    } else {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200.set(inline_handles_200[i], normal_values[i]);
        }
        inline_map_200.clear();
      }
    }
  }
}

BENCHMARK(BM_InlineMapSet)->Args({0, 0})->Args({1, 0})->Args({2, 0})->Args({2, 1});

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_InlineMapGet(benchmark::State& state) {
  const size_t map_type = state.range(0);
  const size_t used_inline_handles = state.range(1);

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
  InlineMap<std::string, std::string> inline_map_200(descriptor_200);
  InlineMap<std::string, std::string> inline_map_0(descriptor_0);

  for (size_t i = 0; i < 200; ++i) {
    normal_map[normal_keys[i]] = "value_" + std::to_string(i);
    inline_map_200.set(normal_keys[i], "value_" + std::to_string(i));
    inline_map_0.set(normal_keys[i], "value_" + std::to_string(i));
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
        inline_map_0.get(normal_keys[i]);
      }
    }
  } else {
    // Inline map with 200 inline keys.
    if (used_inline_handles == 0) {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200.get(normal_keys[i]);
        }
      }
    } else {
      for (auto _ : state) { // NOLINT
        for (size_t i = 0; i < 200; ++i) {
          inline_map_200.get(inline_handles_200[i]);
        }
      }
    }
  }
}

BENCHMARK(BM_InlineMapGet)->Args({0, 0})->Args({1, 0})->Args({2, 0})->Args({2, 1});

} // namespace
} // namespace Envoy
