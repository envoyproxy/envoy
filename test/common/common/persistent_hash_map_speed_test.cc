// This should be run with --compilation_mode=opt, and would benefit from a quiescent system with
// disabled cstate power management.

#include <memory>
#include <string>
#include <vector>

#include "source/common/common/persistent_hash_map.h"

#include "test/benchmark/main.h"

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

// Values are shared pointers so that copying a flat map pays the same reference count traffic as
// copying the cross priority host map does.
using Value = std::shared_ptr<int>;
using FlatMap = absl::flat_hash_map<std::string, Value>;
using PersistentMap =
    PersistentHashMap<std::string, Value, absl::Hash<absl::string_view>, std::equal_to<>>;

std::vector<std::string> makeAddresses(size_t count) {
  std::vector<std::string> addresses;
  addresses.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    addresses.push_back(absl::StrCat("10.", (i >> 16) & 0xff, ".", (i >> 8) & 0xff, ".", i & 0xff,
                                     ":", 8000 + (i >> 24)));
  }
  return addresses;
}

Value makeValue() { return std::make_shared<int>(0); }

// The benchmark test only checks that the benchmarks run, so skip the large maps there.
bool skip(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks() && state.range(0) > 1000) {
    state.SkipWithError("Skipping expensive benchmark");
    return true;
  }
  return false;
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_FlatMapFind(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0));
  FlatMap map;
  for (const std::string& address : addresses) {
    map.emplace(address, makeValue());
  }
  size_t i = 0;
  for (auto _ : state) { // NOLINT
    ::benchmark::DoNotOptimize(map.find(absl::string_view(addresses[i])));
    i = (i + 1) % addresses.size();
  }
}
BENCHMARK(BM_FlatMapFind)->Arg(1000)->Arg(100000)->Arg(1000000);

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PersistentMapFind(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0));
  PersistentMap map;
  for (const std::string& address : addresses) {
    map.insert(address, makeValue());
  }
  size_t i = 0;
  for (auto _ : state) { // NOLINT
    ::benchmark::DoNotOptimize(map.find(absl::string_view(addresses[i])));
    i = (i + 1) % addresses.size();
  }
}
BENCHMARK(BM_PersistentMapFind)->Arg(1000)->Arg(100000)->Arg(1000000);

// Publishing a flat map after a delta of range(1) hosts copies the whole map, as the cross priority
// host map does by default.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_FlatMapUpdateAndPublish(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0) + state.range(1));
  std::shared_ptr<const FlatMap> published;
  {
    FlatMap initial;
    for (int64_t i = 0; i < state.range(0); ++i) {
      initial.emplace(addresses[i], makeValue());
    }
    published = std::make_shared<const FlatMap>(std::move(initial));
  }
  // Each iteration adds the next range(1) addresses and removes the oldest range(1) live ones, so
  // the map keeps range(0) entries.
  int64_t next = state.range(0);
  for (auto _ : state) { // NOLINT
    auto updated = std::make_shared<FlatMap>(*published);
    for (int64_t i = 0; i < state.range(1); ++i) {
      updated->emplace(addresses[(next + i) % addresses.size()], makeValue());
      updated->erase(addresses[(next + i + state.range(1)) % addresses.size()]);
    }
    next = (next + state.range(1)) % addresses.size();
    published = std::move(updated);
  }
}
BENCHMARK(BM_FlatMapUpdateAndPublish)->Args({1000, 10})->Args({100000, 10})->Args({1000000, 10});

// Publishing a persistent map after a delta of range(1) hosts only copies the changed paths.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PersistentMapUpdateAndPublish(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0) + state.range(1));
  PersistentMap map;
  for (int64_t i = 0; i < state.range(0); ++i) {
    map.insert(addresses[i], makeValue());
  }
  auto published = std::make_shared<const PersistentMap>(map);
  int64_t next = state.range(0);
  for (auto _ : state) { // NOLINT
    for (int64_t i = 0; i < state.range(1); ++i) {
      map.insert(addresses[(next + i) % addresses.size()], makeValue());
      map.erase(addresses[(next + i + state.range(1)) % addresses.size()]);
    }
    next = (next + state.range(1)) % addresses.size();
    published = std::make_shared<const PersistentMap>(map);
  }
}
BENCHMARK(BM_PersistentMapUpdateAndPublish)
    ->Args({1000, 10})
    ->Args({100000, 10})
    ->Args({1000000, 10});

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_FlatMapPopulate(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0));
  const Value value = makeValue();
  for (auto _ : state) { // NOLINT
    FlatMap map;
    for (const std::string& address : addresses) {
      map.emplace(address, value);
    }
    ::benchmark::DoNotOptimize(map);
  }
}
BENCHMARK(BM_FlatMapPopulate)->Arg(1000)->Arg(100000)->Arg(1000000);

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PersistentMapPopulate(::benchmark::State& state) {
  if (skip(state)) {
    return;
  }
  const std::vector<std::string> addresses = makeAddresses(state.range(0));
  const Value value = makeValue();
  for (auto _ : state) { // NOLINT
    PersistentMap map;
    for (const std::string& address : addresses) {
      map.insert(address, value);
    }
    ::benchmark::DoNotOptimize(map);
  }
}
BENCHMARK(BM_PersistentMapPopulate)->Arg(1000)->Arg(100000)->Arg(1000000);

} // namespace
} // namespace Envoy
