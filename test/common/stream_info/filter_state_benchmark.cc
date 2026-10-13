#include <memory>

#include "envoy/stream_info/filter_state.h"

#include "source/common/stream_info/filter_state_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace StreamInfo {
namespace {

class SimpleType : public FilterState::Object {
public:
  SimpleType(int value) : value_(value) {}
  int access() const { return value_; }

private:
  int value_;
};

static void BM_FilterStateStringLookup(benchmark::State& state) {
  FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
  const std::string name = "my_custom_key";
  filter_state.setData(name, std::make_shared<SimpleType>(42), FilterState::LifeSpan::FilterChain);

  for (auto _ : state) {
    auto* obj = filter_state.getDataReadOnly<SimpleType>(name);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateStringLookup);

static void BM_FilterStateIndexedLookup(benchmark::State& state) {
  FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));
  filter_state.setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(42),
                              FilterState::LifeSpan::FilterChain);

  for (auto _ : state) {
    auto* obj = filter_state.getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateIndexedLookup);

static void BM_FilterStateStringCycle(benchmark::State& state) {
  const std::string name = "envoy.filters.network.http_connection_manager.local_reply_owner";
  for (auto _ : state) {
    FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
    filter_state.setData(name, std::make_shared<SimpleType>(42),
                         FilterState::LifeSpan::FilterChain);
    auto* obj = filter_state.getDataReadOnly<SimpleType>(name);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateStringCycle);

static void BM_FilterStateIndexedCycle(benchmark::State& state) {
  const std::string name = std::string(FilterState::indexToName(FilterStateIndex::LocalReplyOwner));
  for (auto _ : state) {
    FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
    filter_state.setIndexedData(FilterStateIndex::LocalReplyOwner, std::make_shared<SimpleType>(42),
                                FilterState::LifeSpan::FilterChain);
    auto* obj = filter_state.getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateIndexedCycle);

static void BM_FilterStateStringComparisonPenalty(benchmark::State& state) {
  FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
  // "envoy.network.upstream_server_name" is an indexed key (34 chars).
  // "envoy.network.upstream_server_namx" is a custom string key (34 chars) differing only in the
  // last char.
  const std::string custom_key = "envoy.network.upstream_server_namx";
  filter_state.setData(custom_key, std::make_shared<SimpleType>(42),
                       FilterState::LifeSpan::FilterChain);

  for (auto _ : state) {
    auto* obj = filter_state.getDataReadOnly<SimpleType>(custom_key);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateStringComparisonPenalty);

static void BM_FilterStateIndexedUpstreamServerNameLookup(benchmark::State& state) {
  FilterStateImpl filter_state(FilterState::LifeSpan::FilterChain);
  // "envoy.network.upstream_server_name" is resolved in O(1) via its index without string lookup
  filter_state.setIndexedData(FilterStateIndex::UpstreamServerName,
                              std::make_shared<SimpleType>(42), FilterState::LifeSpan::FilterChain);

  for (auto _ : state) {
    auto* obj =
        filter_state.getIndexedDataReadOnly<SimpleType>(FilterStateIndex::UpstreamServerName);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateIndexedUpstreamServerNameLookup);

} // namespace
} // namespace StreamInfo
} // namespace Envoy
