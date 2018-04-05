#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "common/common/c_smart_ptr.h"
#include "common/stats/thread_local_store.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Stats {

/**
 * This is a heap test allocator that works similar to how the shared memory allocator works in
 * terms of reference counting, etc.
 */
class TestAllocator : public RawStatDataAllocator {
public:
  ~TestAllocator() { EXPECT_TRUE(stats_.empty()); }

  RawStatData* alloc(const std::string& name) override {
    CSmartPtr<RawStatData, freeAdapter>& stat_ref = stats_[name];
    if (!stat_ref) {
      stat_ref.reset(static_cast<RawStatData*>(::calloc(RawStatData::size(), 1)));
      stat_ref->initialize(name);
    } else {
      stat_ref->ref_count_++;
    }

    return stat_ref.get();
  }

  void free(RawStatData& data) override {
    if (--data.ref_count_ > 0) {
      return;
    }

    for (auto i = stats_.begin(); i != stats_.end(); i++) {
      if (i->second.get() == &data) {
        stats_.erase(i);
        return;
      }
    }

    FAIL();
  }

private:
  static void freeAdapter(RawStatData* data) { ::free(data); }
  std::unordered_map<std::string, CSmartPtr<RawStatData, freeAdapter>> stats_;
};

class StatsThreadLocalStoreTest : public testing::Test, public RawStatDataAllocator {
public:
  StatsThreadLocalStoreTest() {
    ON_CALL(*this, alloc(_)).WillByDefault(Invoke([this](const std::string& name) -> RawStatData* {
      return alloc_.alloc(name);
    }));

    ON_CALL(*this, free(_)).WillByDefault(Invoke([this](RawStatData& data) -> void {
      return alloc_.free(data);
    }));

    EXPECT_CALL(*this, alloc("stats.overflow"));
    store_.reset(new ThreadLocalStoreImpl(*this));
    store_->addSink(sink_);
  }

  MOCK_METHOD1(alloc, RawStatData*(const std::string& name));
  MOCK_METHOD1(free, void(RawStatData& data));

  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  TestAllocator alloc_;
  MockSink sink_;
  std::unique_ptr<ThreadLocalStoreImpl> store_;
};

TEST_F(StatsThreadLocalStoreTest, NoTls) {
  InSequence s;
  EXPECT_CALL(*this, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Histogram& h1 = store_->histogram("h1");
  //  EXPECT_EQ(&h1, &store_->histogram("h1"));
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  store_->deliverHistogramToSinks(h1, 100);

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(2L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);

  store_->shutdownThreading();
}

TEST_F(StatsThreadLocalStoreTest, Tls) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Histogram& h1 = store_->histogram("h1");
  EXPECT_EQ(&h1, &store_->histogram("h1"));

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(3L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(3L, store_->gauges().front().use_count());

  store_->shutdownThreading();
  tls_.shutdownThread();

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(2L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, BasicScope) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*this, alloc(_)).Times(4);
  Counter& c1 = store_->counter("c1");
  Counter& c2 = scope1->counter("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());

  Gauge& g1 = store_->gauge("g1");
  Gauge& g2 = scope1->gauge("g2");
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 200));
  h2.recordValue(200);

  store_->shutdownThreading();
  scope1->deliverHistogramToSinks(h1, 100);
  scope1->deliverHistogramToSinks(h2, 200);
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(5);
}

TEST_F(StatsThreadLocalStoreTest, BasicHistogramMerge) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = store_->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 1));
  h2.recordValue(1);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 2));
  h2.recordValue(2);

  std::shared_ptr<std::atomic<bool>> merge_called = std::make_shared<std::atomic<bool>>(false);
  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  EXPECT_TRUE(*merge_called);

  std::list<HistogramSharedPtr> histogram_list = store_->histograms();

  EXPECT_EQ(histogram_list.size(), 2);
  std::string h1_summary = "P0: 1, P25: 1.025, P50: 1.05, P75: 1.075, P90: 1.09, P95: 1.095, P99: "
                           "1.099, P99.9: 1.0999, P100: 1.1";
  std::string h2_summary = "P0: 1, P25: 1.05, P50: 1.1, P75: 2.05, P90: 2.08, P95: 2.09, P99: "
                           "2.098, P99.9: 2.0998, P100: 2.1";

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    EXPECT_EQ(histogram->cumulativeStatistics().summary(),
              histogram->intervalStatistics().summary());
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), h1_summary);
    } else {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), h2_summary);
    }
  }

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, MultipleMerges) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = store_->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  // Test Merge with inserting one value in to histogram and validate that summary returns correct
  // values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  std::shared_ptr<std::atomic<bool>> merge_called = std::make_shared<std::atomic<bool>>(false);

  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  EXPECT_TRUE(*merge_called);

  std::list<HistogramSharedPtr> histogram_list = store_->histograms();

  EXPECT_EQ(histogram_list.size(), 2);

  std::string summary_with_one_as_value =
      "P0: 1, P25: 1.025, P50: 1.05, P75: 1.075, P90: 1.09, P95: 1.095, P99: "
      "1.099, P99.9: 1.0999, P100: 1.1";
  std::string summary_with_two_as_value = "P0: 1, P25: 2.025, P50: 2.05, P75: 2.075, P90: 2.09, "
                                          "P95: 2.095, P99: 2.099, P99.9: 2.0999, P100: 2.1";
  std::string not_used_summary =
      "P0: 0, P25: 0, P50: 0, P75: 0, P90: 0, P95: 0, P99: 0, P99.9: 0, P100: 0";

  std::string summary_with_two_values =
      "P0: 1, P25: 1.05, P50: 1.1, P75: 2.05, P90: 2.08, P95: 2.09, P99: "
      "2.098, P99.9: 2.0998, P100: 2.1";

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    EXPECT_EQ(histogram->cumulativeStatistics().summary(),
              histogram->intervalStatistics().summary());
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    } else {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), not_used_summary);
    }
  }

  merge_called = std::make_shared<std::atomic<bool>>(false);

  // Insert value into second histogram and validate that it is merged properly.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 1));
  h2.recordValue(1);

  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  histogram_list = store_->histograms();
  EXPECT_EQ(histogram_list.size(), 2);
  EXPECT_TRUE(*merge_called);

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_NE(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->intervalStatistics().summary(), not_used_summary);
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    } else {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    }
  }

  merge_called = std::make_shared<std::atomic<bool>>(false);

  // Insert second value into second histogram and validate that it is merged properly.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 2));
  h2.recordValue(2);

  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  histogram_list = store_->histograms();
  EXPECT_EQ(histogram_list.size(), 2);
  EXPECT_TRUE(*merge_called);

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_NE(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->intervalStatistics().summary(), not_used_summary);
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    } else {
      EXPECT_NE(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->intervalStatistics().summary(), summary_with_two_as_value);
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_two_values);
    }
  }

  // Do not insert any value and validate that intervalSummary is empty for both the histograms and
  // cumulativeSummary has right values.
  merge_called = std::make_shared<std::atomic<bool>>(false);
  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  histogram_list = store_->histograms();
  EXPECT_EQ(histogram_list.size(), 2);
  EXPECT_TRUE(*merge_called);

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_NE(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->intervalStatistics().summary(), not_used_summary);
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    } else {
      EXPECT_NE(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->intervalStatistics().summary(), not_used_summary);
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_two_values);
    }
  }
  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, BasicScopeHistogramMerge) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 2));
  h2.recordValue(2);

  std::shared_ptr<std::atomic<bool>> merge_called = std::make_shared<std::atomic<bool>>(false);

  store_->mergeHistograms([merge_called]() -> void { *merge_called = true; });

  EXPECT_TRUE(*merge_called);

  std::list<HistogramSharedPtr> histogram_list = store_->histograms();

  EXPECT_EQ(histogram_list.size(), 2);

  std::string summary_with_one_as_value =
      "P0: 1, P25: 1.025, P50: 1.05, P75: 1.075, P90: 1.09, P95: 1.095, P99: "
      "1.099, P99.9: 1.0999, P100: 1.1";
  std::string summary_with_two_as_value = "P0: 2, P25: 2.025, P50: 2.05, P75: 2.075, P90: 2.09, "
                                          "P95: 2.095, P99: 2.099, P99.9: 2.0999, P100: 2.1";

  for (const Stats::HistogramSharedPtr& histogram : histogram_list) {
    if (histogram->name().find("h1") != std::string::npos) {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_one_as_value);
    } else {
      EXPECT_EQ(histogram->cumulativeStatistics().summary(),
                histogram->intervalStatistics().summary());
      EXPECT_EQ(histogram->cumulativeStatistics().summary(), summary_with_two_as_value);
    }
  }

  store_->shutdownThreading();

  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, ScopeDelete) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*this, alloc(_));
  scope1->counter("c1");
  EXPECT_EQ(2UL, store_->counters().size());
  CounterSharedPtr c1 = store_->counters().front();
  EXPECT_EQ("scope1.c1", c1->name());

  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(tls_, runOnAllThreads(_));
  scope1.reset();
  EXPECT_EQ(1UL, store_->counters().size());

  EXPECT_CALL(*this, free(_));
  EXPECT_EQ(1L, c1.use_count());
  c1.reset();

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, NestedScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*this, alloc(_));
  Counter& c1 = scope1->counter("foo.bar");
  EXPECT_EQ("scope1.foo.bar", c1.name());

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_CALL(*this, alloc(_));
  Counter& c2 = scope2->counter("bar");
  EXPECT_NE(&c1, &c2);
  EXPECT_EQ("scope1.foo.bar", c2.name());

  // Different allocations point to the same referenced counted backing memory.
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(c1.value(), c2.value());

  EXPECT_CALL(*this, alloc(_));
  Gauge& g1 = scope2->gauge("some_gauge");
  EXPECT_EQ("scope1.foo.some_gauge", g1.name());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(4);
}

TEST_F(StatsThreadLocalStoreTest, OverlappingScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Both scopes point to the same namespace. This can happen during reload of a cluster for
  // example.
  ScopePtr scope1 = store_->createScope("scope1.");
  ScopePtr scope2 = store_->createScope("scope1.");

  // We will call alloc twice, but they should point to the same backing storage.
  EXPECT_CALL(*this, alloc(_)).Times(2);
  Counter& c1 = scope1->counter("c");
  Counter& c2 = scope2->counter("c");
  EXPECT_NE(&c1, &c2);
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(1UL, c2.value());
  c2.inc();
  EXPECT_EQ(2UL, c1.value());
  EXPECT_EQ(2UL, c2.value());

  // We should dedup when we fetch all counters to handle the overlapping case.
  EXPECT_EQ(2UL, store_->counters().size());

  // Gauges should work the same way.
  EXPECT_CALL(*this, alloc(_)).Times(2);
  Gauge& g1 = scope1->gauge("g");
  Gauge& g2 = scope2->gauge("g");
  EXPECT_NE(&g1, &g2);
  g1.set(5);
  EXPECT_EQ(5UL, g1.value());
  EXPECT_EQ(5UL, g2.value());
  g2.set(1);
  EXPECT_EQ(1UL, g1.value());
  EXPECT_EQ(1UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());

  // Deleting scope 1 will call free but will be reference counted. It still leaves scope 2 valid.
  EXPECT_CALL(*this, free(_)).Times(2);
  scope1.reset();
  c2.inc();
  EXPECT_EQ(3UL, c2.value());
  EXPECT_EQ(2UL, store_->counters().size());
  g2.set(10);
  EXPECT_EQ(10UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, AllocFailed) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc("foo")).WillOnce(Return(nullptr));
  Counter& c1 = store_->counter("foo");
  EXPECT_EQ(1UL, store_->counter("stats.overflow").value());

  c1.inc();
  EXPECT_EQ(1UL, c1.value());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow but not the failsafe stat which we allocated from the heap.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, ShuttingDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc(_)).Times(4);
  store_->counter("c1");
  store_->gauge("g1");
  store_->shutdownThreading();
  store_->counter("c2");
  store_->gauge("g2");

  // c1, g1 should have a thread local ref, but c2, g2 should not.
  EXPECT_EQ(3L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(3L, TestUtility::findGauge(*store_, "g1").use_count());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c2").use_count());
  EXPECT_EQ(2L, TestUtility::findGauge(*store_, "g2").use_count());

  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(5);
}

} // namespace Stats
} // namespace Envoy
