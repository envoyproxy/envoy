#include "source/common/protobuf/utility.h"

#include "test/benchmark/main.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Upstream {
namespace {

// Build a realistic EDS metadata proto with the given number of filter metadata entries.
envoy::config::core::v3::Metadata buildMetadata(int num_filters, int num_fields_per_filter) {
  envoy::config::core::v3::Metadata metadata;
  for (int i = 0; i < num_filters; i++) {
    auto* fields =
        (*metadata.mutable_filter_metadata())[absl::StrCat("envoy.filter.", i)].mutable_fields();
    for (int j = 0; j < num_fields_per_filter; j++) {
      (*fields)[absl::StrCat("key_", j)].set_string_value(absl::StrCat("value_", j));
    }
  }
  return metadata;
}

// Benchmark Equivalent() for identical metadata.
void bmEquivalentIdentical(::benchmark::State& state) {
  const int num_filters = state.range(0);
  const int num_fields = state.range(1);
  auto metadata1 = buildMetadata(num_filters, num_fields);
  auto metadata2 = buildMetadata(num_filters, num_fields);

  for (auto _ : state) { // NOLINT
    bool result = Protobuf::util::MessageDifferencer::Equivalent(metadata1, metadata2);
    ::benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmEquivalentIdentical)->Args({1, 5})->Args({3, 10})->Args({5, 20})->Args({10, 50});

// Benchmark MessageUtil::hash for identical metadata.
void bmHashIdentical(::benchmark::State& state) {
  const int num_filters = state.range(0);
  const int num_fields = state.range(1);
  auto metadata1 = buildMetadata(num_filters, num_fields);
  auto metadata2 = buildMetadata(num_filters, num_fields);

  for (auto _ : state) { // NOLINT
    bool result = MessageUtil::hash(metadata1) != MessageUtil::hash(metadata2);
    ::benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmHashIdentical)->Args({1, 5})->Args({3, 10})->Args({5, 20})->Args({10, 50});

// Benchmark Equivalent() for different metadata.
void bmEquivalentDifferent(::benchmark::State& state) {
  const int num_filters = state.range(0);
  const int num_fields = state.range(1);
  auto metadata1 = buildMetadata(num_filters, num_fields);
  auto metadata2 = buildMetadata(num_filters, num_fields);
  // Modify one field to make them different.
  (*(*metadata2.mutable_filter_metadata())["envoy.filter.0"].mutable_fields())["key_0"]
      .set_string_value("changed");

  for (auto _ : state) { // NOLINT
    bool result = Protobuf::util::MessageDifferencer::Equivalent(metadata1, metadata2);
    ::benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmEquivalentDifferent)->Args({1, 5})->Args({3, 10})->Args({5, 20})->Args({10, 50});

// Benchmark MessageUtil::hash for different metadata.
void bmHashDifferent(::benchmark::State& state) {
  const int num_filters = state.range(0);
  const int num_fields = state.range(1);
  auto metadata1 = buildMetadata(num_filters, num_fields);
  auto metadata2 = buildMetadata(num_filters, num_fields);
  // Modify one field to make them different.
  (*(*metadata2.mutable_filter_metadata())["envoy.filter.0"].mutable_fields())["key_0"]
      .set_string_value("changed");

  for (auto _ : state) { // NOLINT
    bool result = MessageUtil::hash(metadata1) != MessageUtil::hash(metadata2);
    ::benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(bmHashDifferent)->Args({1, 5})->Args({3, 10})->Args({5, 20})->Args({10, 50});

// Simulate updateDynamicHostList: compare N hosts' metadata.
// This is the realistic scenario where we compare metadata for each host in an EDS update.
void bmUpdateHostListEquivalent(::benchmark::State& state) {
  const int num_hosts = state.range(0);
  const int num_fields = state.range(1);
  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 1000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }
  std::vector<envoy::config::core::v3::Metadata> existing;
  std::vector<envoy::config::core::v3::Metadata> incoming;
  existing.reserve(num_hosts);
  incoming.reserve(num_hosts);
  for (int i = 0; i < num_hosts; i++) {
    existing.push_back(buildMetadata(1, num_fields));
    incoming.push_back(buildMetadata(1, num_fields));
  }

  for (auto _ : state) { // NOLINT
    int changed = 0;
    for (int i = 0; i < num_hosts; i++) {
      if (!Protobuf::util::MessageDifferencer::Equivalent(existing[i], incoming[i])) {
        changed++;
      }
    }
    ::benchmark::DoNotOptimize(changed);
  }
}
BENCHMARK(bmUpdateHostListEquivalent)
    ->Args({100, 5})
    ->Args({1000, 5})
    ->Args({5000, 5})
    ->Args({5000, 20})
    ->Unit(::benchmark::kMillisecond);

void bmUpdateHostListHash(::benchmark::State& state) {
  const int num_hosts = state.range(0);
  const int num_fields = state.range(1);
  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 1000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }
  std::vector<envoy::config::core::v3::Metadata> existing;
  std::vector<envoy::config::core::v3::Metadata> incoming;
  existing.reserve(num_hosts);
  incoming.reserve(num_hosts);
  for (int i = 0; i < num_hosts; i++) {
    existing.push_back(buildMetadata(1, num_fields));
    incoming.push_back(buildMetadata(1, num_fields));
  }

  for (auto _ : state) { // NOLINT
    int changed = 0;
    for (int i = 0; i < num_hosts; i++) {
      if (MessageUtil::hash(existing[i]) != MessageUtil::hash(incoming[i])) {
        changed++;
      }
    }
    ::benchmark::DoNotOptimize(changed);
  }
}
BENCHMARK(bmUpdateHostListHash)
    ->Args({100, 5})
    ->Args({1000, 5})
    ->Args({5000, 5})
    ->Args({5000, 20})
    ->Unit(::benchmark::kMillisecond);

// Simulate updateDynamicHostList with cached hashes (pre-computed at metadata set time).
// This is the approach used in the actual implementation.
void bmUpdateHostListCachedHash(::benchmark::State& state) {
  const int num_hosts = state.range(0);
  const int num_fields = state.range(1);
  if (benchmark::skipExpensiveBenchmarks() && num_hosts > 1000) {
    state.SkipWithError("Skipping expensive benchmark");
    return;
  }
  std::vector<std::size_t> existing_hashes;
  std::vector<std::size_t> incoming_hashes;
  existing_hashes.reserve(num_hosts);
  incoming_hashes.reserve(num_hosts);
  for (int i = 0; i < num_hosts; i++) {
    existing_hashes.push_back(MessageUtil::hash(buildMetadata(1, num_fields)));
    incoming_hashes.push_back(MessageUtil::hash(buildMetadata(1, num_fields)));
  }

  for (auto _ : state) { // NOLINT
    int changed = 0;
    for (int i = 0; i < num_hosts; i++) {
      if (existing_hashes[i] != incoming_hashes[i]) {
        changed++;
      }
    }
    ::benchmark::DoNotOptimize(changed);
  }
}
BENCHMARK(bmUpdateHostListCachedHash)
    ->Args({100, 5})
    ->Args({1000, 5})
    ->Args({5000, 5})
    ->Args({5000, 20})
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace Upstream
} // namespace Envoy
