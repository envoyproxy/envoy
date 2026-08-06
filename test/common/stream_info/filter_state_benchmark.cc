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
  filter_state.setIndexedData(FilterStateIndex::LocalReplyOwner, name,
                              std::make_shared<SimpleType>(42), FilterState::LifeSpan::FilterChain);

  for (auto _ : state) {
    auto* obj = filter_state.getIndexedDataReadOnly<SimpleType>(FilterStateIndex::LocalReplyOwner);
    benchmark::DoNotOptimize(obj);
  }
}
BENCHMARK(BM_FilterStateIndexedLookup);

} // namespace
} // namespace StreamInfo
} // namespace Envoy
