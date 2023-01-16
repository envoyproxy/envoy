#pragma once

#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/grouped_stats_request.h"
#include "source/server/admin/ungrouped_stats_request.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

template <class T> class StatsRequestTestBase : public testing::Test {
protected:
  StatsRequestTestBase();
  ~StatsRequestTestBase() override;

  uint32_t iterateChunks(T& request, bool drain = true, Http::Code expect_code = Http::Code::OK);

  Stats::StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  std::string response(T& request);

  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl alloc_;
  testing::NiceMock<Stats::MockSink> sink_;
  testing::NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::ThreadLocalStoreImpl store_;
  Buffer::OwnedImpl response_;
};

class TestScope : public Stats::IsolatedScopeImpl {
public:
  TestScope(const std::string& prefix, Stats::MockStore& store);

  bool iterate(const Stats::IterateFn<Stats::Histogram>& fn) const override {
    std::for_each(histograms_.begin(), histograms_.end(), fn);
    return true;
  }

  bool iterate(const Stats::IterateFn<Stats::Counter>& fn) const override {
    std::for_each(counters_.begin(), counters_.end(), fn);
    return true;
  }

  bool iterate(const Stats::IterateFn<Stats::Gauge>& fn) const override {
    std::for_each(gauges_.begin(), gauges_.end(), fn);
    return true;
  }

  bool iterate(const Stats::IterateFn<Stats::TextReadout>& fn) const override {
    std::for_each(text_readouts_.begin(), text_readouts_.end(), fn);
    return true;
  }

public:
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::TextReadoutSharedPtr> text_readouts_;
  ;
};

class MockStore : public Stats::MockStore {
public:
  MOCK_METHOD(Stats::ScopeSharedPtr, rootScope, ());
};

} // namespace Server
} // namespace Envoy
