#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/histogram.h"

#include "common/common/c_smart_ptr.h"
#include "common/event/dispatcher_impl.h"
#include "common/memory/stats.h"
#include "common/stats/stats_matcher_impl.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stats/tag_producer_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_split.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Stats {

const uint64_t MaxStatNameLength = 127;

class ThreadLocalStoreTestingPeer {
public:
  // Calculates the number of TLS histograms across all threads. This requires
  // dispatching to all threads and blocking on their completion, and is exposed
  // as a testing peer to enable tests that ensure that TLS histograms don't
  // leak.
  //
  // Note that this must be called from the "main thread", which has different
  // implications for unit tests that use real threads vs mocks. The easiest way
  // to capture this in a general purpose helper is to use a callback to convey
  // the resultant sum.
  static void numTlsHistograms(ThreadLocalStoreImpl& thread_local_store_impl,
                               const std::function<void(uint32_t)>& num_tls_hist_cb) {
    auto num_tls_histograms = std::make_shared<std::atomic<uint32_t>>(0);
    thread_local_store_impl.tls_->runOnAllThreads(
        [num_tls_histograms](ThreadLocal::ThreadLocalObjectSharedPtr object)
            -> ThreadLocal::ThreadLocalObjectSharedPtr {
          auto& tls_cache = object->asType<ThreadLocalStoreImpl::TlsCache>();
          *num_tls_histograms += tls_cache.tls_histogram_cache_.size();
          return object;
        },
        [num_tls_hist_cb, num_tls_histograms]() { num_tls_hist_cb(*num_tls_histograms); });
  }
};

class StatsThreadLocalStoreTest : public testing::Test {
public:
  StatsThreadLocalStoreTest()
      : alloc_(symbol_table_), store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)) {
    store_->addSink(sink_);
  }

  void resetStoreWithAlloc(Allocator& alloc) {
    store_ = std::make_unique<ThreadLocalStoreImpl>(alloc);
    store_->addSink(sink_);
  }

  uint32_t numTlsHistograms() {
    uint32_t num_tls_histograms;
    absl::Mutex mutex;
    bool done = false;
    ThreadLocalStoreTestingPeer::numTlsHistograms(
        *store_, [&mutex, &done, &num_tls_histograms](uint32_t num) {
          absl::MutexLock lock(&mutex);
          num_tls_histograms = num;
          done = true;
        });
    absl::MutexLock lock(&mutex);
    mutex.Await(absl::Condition(&done));
    return num_tls_histograms;
  }

  SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  AllocatorImpl alloc_;
  MockSink sink_;
  ThreadLocalStoreImplPtr store_;
};

class HistogramWrapper {
public:
  HistogramWrapper() : histogram_(hist_alloc()) {}

  ~HistogramWrapper() { hist_free(histogram_); }

  const histogram_t* getHistogram() { return histogram_; }

  void setHistogramValues(const std::vector<uint64_t>& values) {
    for (uint64_t value : values) {
      hist_insert_intscale(histogram_, value, 0, 1);
    }
  }

private:
  histogram_t* histogram_;
};

class HistogramTest : public testing::Test {
public:
  using NameHistogramMap = std::map<std::string, ParentHistogramSharedPtr>;

  HistogramTest() : alloc_(symbol_table_) {}

  void SetUp() override {
    store_ = std::make_unique<ThreadLocalStoreImpl>(alloc_);
    store_->addSink(sink_);
    store_->initializeThreading(main_thread_dispatcher_, tls_);
  }

  void TearDown() override {
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  NameHistogramMap makeHistogramMap(const std::vector<ParentHistogramSharedPtr>& hist_list) {
    NameHistogramMap name_histogram_map;
    for (const ParentHistogramSharedPtr& histogram : hist_list) {
      // Exclude the scope part of the name.
      const std::vector<std::string>& split_vector = absl::StrSplit(histogram->name(), '.');
      name_histogram_map.insert(std::make_pair(split_vector.back(), histogram));
    }
    return name_histogram_map;
  }

  /**
   * Validates that Histogram merge happens as desired and returns the processed histogram count
   * that can be asserted later.
   */
  uint64_t validateMerge() {
    bool merge_called = false;
    store_->mergeHistograms([&merge_called]() -> void { merge_called = true; });

    EXPECT_TRUE(merge_called);

    std::vector<ParentHistogramSharedPtr> histogram_list = store_->histograms();

    HistogramWrapper hist1_cumulative;
    HistogramWrapper hist2_cumulative;
    HistogramWrapper hist1_interval;
    HistogramWrapper hist2_interval;

    hist1_cumulative.setHistogramValues(h1_cumulative_values_);
    hist2_cumulative.setHistogramValues(h2_cumulative_values_);
    hist1_interval.setHistogramValues(h1_interval_values_);
    hist2_interval.setHistogramValues(h2_interval_values_);

    HistogramStatisticsImpl h1_cumulative_statistics(hist1_cumulative.getHistogram());
    HistogramStatisticsImpl h2_cumulative_statistics(hist2_cumulative.getHistogram());
    HistogramStatisticsImpl h1_interval_statistics(hist1_interval.getHistogram());
    HistogramStatisticsImpl h2_interval_statistics(hist2_interval.getHistogram());

    NameHistogramMap name_histogram_map = makeHistogramMap(histogram_list);
    const ParentHistogramSharedPtr& h1 = name_histogram_map["h1"];
    EXPECT_EQ(h1->cumulativeStatistics().quantileSummary(),
              h1_cumulative_statistics.quantileSummary());
    EXPECT_EQ(h1->intervalStatistics().quantileSummary(), h1_interval_statistics.quantileSummary());
    EXPECT_EQ(h1->cumulativeStatistics().bucketSummary(), h1_cumulative_statistics.bucketSummary());
    EXPECT_EQ(h1->intervalStatistics().bucketSummary(), h1_interval_statistics.bucketSummary());

    if (histogram_list.size() > 1) {
      const ParentHistogramSharedPtr& h2 = name_histogram_map["h2"];
      EXPECT_EQ(h2->cumulativeStatistics().quantileSummary(),
                h2_cumulative_statistics.quantileSummary());
      EXPECT_EQ(h2->intervalStatistics().quantileSummary(),
                h2_interval_statistics.quantileSummary());
      EXPECT_EQ(h2->cumulativeStatistics().bucketSummary(),
                h2_cumulative_statistics.bucketSummary());
      EXPECT_EQ(h2->intervalStatistics().bucketSummary(), h2_interval_statistics.bucketSummary());
    }

    h1_interval_values_.clear();
    h2_interval_values_.clear();

    return histogram_list.size();
  }

  void expectCallAndAccumulate(Histogram& histogram, uint64_t record_value) {
    EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), record_value));
    histogram.recordValue(record_value);

    if (histogram.name() == "h1") {
      h1_cumulative_values_.push_back(record_value);
      h1_interval_values_.push_back(record_value);
    } else {
      h2_cumulative_values_.push_back(record_value);
      h2_interval_values_.push_back(record_value);
    }
  }

  SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  AllocatorImpl alloc_;
  MockSink sink_;
  ThreadLocalStoreImplPtr store_;
  InSequence s;
  std::vector<uint64_t> h1_cumulative_values_, h2_cumulative_values_, h1_interval_values_,
      h2_interval_values_;
};

TEST_F(StatsThreadLocalStoreTest, NoTls) {
  InSequence s;

  Counter& c1 = store_->counterFromString("c1");
  EXPECT_EQ(&c1, &store_->counterFromString("c1"));
  StatNameManagedStorage c1_name("c1", symbol_table_);
  c1.add(100);
  auto found_counter = store_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&g1, &store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate));
  StatNameManagedStorage g1_name("g1", symbol_table_);
  g1.set(100);
  auto found_gauge = store_->findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(&h1, &store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified));
  StatNameManagedStorage h1_name("h1", symbol_table_);
  auto found_histogram = store_->findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());

  TextReadout& t1 = store_->textReadoutFromString("t1");
  EXPECT_EQ(&t1, &store_->textReadoutFromString("t1"));

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  store_->deliverHistogramToSinks(h1, 100);

  EXPECT_EQ(1UL, store_->counters().size());
  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());
  EXPECT_EQ(1UL, store_->textReadouts().size());
  EXPECT_EQ(&t1, store_->textReadouts().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->textReadouts().front().use_count());

  store_->shutdownThreading();
}

TEST_F(StatsThreadLocalStoreTest, Tls) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Counter& c1 = store_->counterFromString("c1");
  EXPECT_EQ(&c1, &store_->counterFromString("c1"));
  StatNameManagedStorage c1_name("c1", symbol_table_);
  c1.add(100);
  auto found_counter = store_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&g1, &store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate));
  StatNameManagedStorage g1_name("g1", symbol_table_);
  g1.set(100);
  auto found_gauge = store_->findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(&h1, &store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified));
  StatNameManagedStorage h1_name("h1", symbol_table_);
  auto found_histogram = store_->findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());

  TextReadout& t1 = store_->textReadoutFromString("t1");
  EXPECT_EQ(&t1, &store_->textReadoutFromString("t1"));

  EXPECT_EQ(1UL, store_->counters().size());

  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());
  EXPECT_EQ(1UL, store_->textReadouts().size());
  EXPECT_EQ(&t1, store_->textReadouts().front().get()); // front() ok when size()==1
  EXPECT_EQ(2UL, store_->textReadouts().front().use_count());

  store_->shutdownThreading();
  tls_.shutdownThread();

  EXPECT_EQ(1UL, store_->counters().size());
  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());
  EXPECT_EQ(1UL, store_->textReadouts().size());
  EXPECT_EQ(&t1, store_->textReadouts().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->textReadouts().front().use_count());
}

TEST_F(StatsThreadLocalStoreTest, BasicScope) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  Counter& c1 = store_->counterFromString("c1");
  Counter& c2 = scope1->counterFromString("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  StatNameManagedStorage c1_name("c1", symbol_table_);
  auto found_counter = store_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  StatNameManagedStorage c2_name("scope1.c2", symbol_table_);
  auto found_counter2 = store_->findCounter(c2_name.statName());
  ASSERT_TRUE(found_counter2.has_value());
  EXPECT_EQ(&c2, &found_counter2->get());

  Gauge& g1 = store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  StatNameManagedStorage g1_name("g1", symbol_table_);
  auto found_gauge = store_->findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  StatNameManagedStorage g2_name("scope1.g2", symbol_table_);
  auto found_gauge2 = store_->findGauge(g2_name.statName());
  ASSERT_TRUE(found_gauge2.has_value());
  EXPECT_EQ(&g2, &found_gauge2->get());

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 200));
  h2.recordValue(200);
  StatNameManagedStorage h1_name("h1", symbol_table_);
  auto found_histogram = store_->findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());
  StatNameManagedStorage h2_name("scope1.h2", symbol_table_);
  auto found_histogram2 = store_->findHistogram(h2_name.statName());
  ASSERT_TRUE(found_histogram2.has_value());
  EXPECT_EQ(&h2, &found_histogram2->get());

  TextReadout& t1 = store_->textReadoutFromString("t1");
  TextReadout& t2 = scope1->textReadoutFromString("t2");
  EXPECT_EQ("t1", t1.name());
  EXPECT_EQ("scope1.t2", t2.name());

  StatNameManagedStorage tag_key("a", symbol_table_);
  StatNameManagedStorage tag_value("b", symbol_table_);
  StatNameTagVector tags{{StatName(tag_key.statName()), StatName(tag_value.statName())}};

  const TagVector expectedTags = {Tag{"a", "b"}};

  {
    StatNameManagedStorage storage("c3", symbol_table_);
    Counter& counter = scope1->counterFromStatNameWithTags(StatName(storage.statName()), tags);
    EXPECT_EQ(expectedTags, counter.tags());
    EXPECT_EQ(&counter, &scope1->counterFromStatNameWithTags(StatName(storage.statName()), tags));
  }
  {
    StatNameManagedStorage storage("g3", symbol_table_);
    Gauge& gauge = scope1->gaugeFromStatNameWithTags(StatName(storage.statName()), tags,
                                                     Gauge::ImportMode::Accumulate);
    EXPECT_EQ(expectedTags, gauge.tags());
    EXPECT_EQ(&gauge, &scope1->gaugeFromStatNameWithTags(StatName(storage.statName()), tags,
                                                         Gauge::ImportMode::Accumulate));
  }
  {
    StatNameManagedStorage storage("h3", symbol_table_);
    Histogram& histogram = scope1->histogramFromStatNameWithTags(
        StatName(storage.statName()), tags, Stats::Histogram::Unit::Unspecified);
    EXPECT_EQ(expectedTags, histogram.tags());
    EXPECT_EQ(&histogram,
              &scope1->histogramFromStatNameWithTags(StatName(storage.statName()), tags,
                                                     Stats::Histogram::Unit::Unspecified));
  }

  store_->shutdownThreading();
  scope1->deliverHistogramToSinks(h1, 100);
  scope1->deliverHistogramToSinks(h2, 200);
  scope1.reset();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, HistogramScopeOverlap) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Creating two scopes with the same name gets you two distinct scope objects.
  ScopePtr scope1 = store_->createScope("scope.");
  ScopePtr scope2 = store_->createScope("scope.");
  EXPECT_NE(scope1, scope2);

  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, numTlsHistograms());

  // However, stats created in the two same-named scopes will be the same objects.
  Counter& counter = scope1->counterFromString("counter");
  EXPECT_EQ(&counter, &scope2->counterFromString("counter"));
  Gauge& gauge = scope1->gaugeFromString("gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&gauge, &scope2->gaugeFromString("gauge", Gauge::ImportMode::Accumulate));
  TextReadout& text_readout = scope1->textReadoutFromString("tr");
  EXPECT_EQ(&text_readout, &scope2->textReadoutFromString("tr"));
  Histogram& histogram = scope1->histogramFromString("histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(&histogram, &scope2->histogramFromString("histogram", Histogram::Unit::Unspecified));

  // The histogram was created in scope1, which can now be destroyed. But the
  // histogram is kept alive by scope2.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), 100));
  histogram.recordValue(100);
  EXPECT_EQ(1, store_->histograms().size());
  EXPECT_EQ(1, numTlsHistograms());
  scope1.reset();
  EXPECT_EQ(1, store_->histograms().size());
  EXPECT_EQ(1, numTlsHistograms());
  EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), 200));
  histogram.recordValue(200);
  EXPECT_EQ(&histogram, &scope2->histogramFromString("histogram", Histogram::Unit::Unspecified));
  scope2.reset();
  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, numTlsHistograms());

  store_->shutdownThreading();

  store_->histogramFromString("histogram_after_shutdown", Histogram::Unit::Unspecified);

  tls_.shutdownThread();
}

// Validate that we sanitize away bad characters in the stats prefix.
TEST_F(StatsThreadLocalStoreTest, SanitizePrefix) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope(std::string("scope1:\0:foo.", 13));
  Counter& c1 = scope1->counterFromString("c1");
  EXPECT_EQ("scope1___foo.c1", c1.name());

  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, ConstSymtabAccessor) {
  ScopePtr scope = store_->createScope("scope.");
  const Scope& cscope = *scope;
  const SymbolTable& const_symbol_table = cscope.constSymbolTable();
  SymbolTable& symbol_table = scope->symbolTable();
  EXPECT_EQ(&const_symbol_table, &symbol_table);
}

TEST_F(StatsThreadLocalStoreTest, ScopeDelete) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  scope1->counterFromString("c1");
  EXPECT_EQ(1UL, store_->counters().size());
  CounterSharedPtr c1 = TestUtility::findCounter(*store_, "scope1.c1");
  EXPECT_EQ("scope1.c1", c1->name());

  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(tls_, runOnAllThreads(_, _));
  scope1.reset();
  EXPECT_EQ(0UL, store_->counters().size());

  EXPECT_EQ(1L, c1.use_count());
  c1.reset();

  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, NestedScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  Counter& c1 = scope1->counterFromString("foo.bar");
  EXPECT_EQ("scope1.foo.bar", c1.name());
  StatNameManagedStorage c1_name("scope1.foo.bar", symbol_table_);
  auto found_counter = store_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());

  ScopePtr scope2 = scope1->createScope("foo.");
  Counter& c2 = scope2->counterFromString("bar");
  EXPECT_EQ(&c1, &c2);
  EXPECT_EQ("scope1.foo.bar", c2.name());
  StatNameManagedStorage c2_name("scope1.foo.bar", symbol_table_);
  auto found_counter2 = store_->findCounter(c2_name.statName());
  ASSERT_TRUE(found_counter2.has_value());

  // Different allocations point to the same referenced counted backing memory.
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(c1.value(), c2.value());

  Gauge& g1 = scope2->gaugeFromString("some_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope1.foo.some_gauge", g1.name());

  TextReadout& t1 = scope2->textReadoutFromString("some_string");
  EXPECT_EQ("scope1.foo.some_string", t1.name());

  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, OverlappingScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Both scopes point to the same namespace. This can happen during reload of a cluster for
  // example.
  ScopePtr scope1 = store_->createScope("scope1.");
  ScopePtr scope2 = store_->createScope("scope1.");

  // We will call alloc twice, but they should point to the same backing storage.
  Counter& c1 = scope1->counterFromString("c");
  Counter& c2 = scope2->counterFromString("c");
  EXPECT_EQ(&c1, &c2);
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(1UL, c2.value());
  c2.inc();
  EXPECT_EQ(2UL, c1.value());
  EXPECT_EQ(2UL, c2.value());

  // We should dedup when we fetch all counters to handle the overlapping case.
  EXPECT_EQ(1UL, store_->counters().size());

  // Gauges should work the same way.
  Gauge& g1 = scope1->gaugeFromString("g", Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope2->gaugeFromString("g", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&g1, &g2);
  g1.set(5);
  EXPECT_EQ(5UL, g1.value());
  EXPECT_EQ(5UL, g2.value());
  g2.set(1);
  EXPECT_EQ(1UL, g1.value());
  EXPECT_EQ(1UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());

  // TextReadouts should work just like gauges.
  TextReadout& t1 = scope1->textReadoutFromString("b");
  TextReadout& t2 = scope2->textReadoutFromString("b");
  EXPECT_EQ(&t1, &t2);

  t1.set("hello");
  EXPECT_EQ("hello", t1.value());
  EXPECT_EQ("hello", t2.value());
  t2.set("goodbye");
  EXPECT_EQ("goodbye", t1.value());
  EXPECT_EQ("goodbye", t2.value());
  EXPECT_EQ(1UL, store_->textReadouts().size());

  // Deleting scope 1 will call free but will be reference counted. It still leaves scope 2 valid.
  scope1.reset();
  c2.inc();
  EXPECT_EQ(3UL, c2.value());
  EXPECT_EQ(1UL, store_->counters().size());
  g2.set(10);
  EXPECT_EQ(10UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());
  t2.set("abc");
  EXPECT_EQ("abc", t2.value());
  EXPECT_EQ(1UL, store_->textReadouts().size());

  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, TextReadoutAllLengths) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  TextReadout& t = store_->textReadoutFromString("t");
  EXPECT_EQ("", t.value());
  std::string str;
  // ASCII
  for (int i = 0; i < 15; i++) {
    str += ('a' + i);
    t.set(std::string(str));
    EXPECT_EQ(str, t.value());
  }

  // Non-ASCII
  str = "";
  for (int i = 0; i < 15; i++) {
    str += ('\xEE' + i);
    t.set(std::string(str));
    EXPECT_EQ(str, t.value());
  }

  // Null bytes ok; the TextReadout implementation doesn't use null termination in its storage
  t.set(std::string("\x00", 1));
  EXPECT_EQ(std::string("\x00", 1), t.value());
  t.set(std::string("\x00\x00\x00", 3));
  EXPECT_EQ(std::string("\x00\x00\x00", 3), t.value());
  EXPECT_NE(std::string("\x00", 1), t.value());
  EXPECT_NE(std::string("", 0), t.value());

  // No Truncation to 15
  t.set("aaaabbbbccccdddX");
  EXPECT_EQ("aaaabbbbccccdddX", t.value());
  t.set("aaaabbbbccccdddXX");
  EXPECT_EQ("aaaabbbbccccdddXX", t.value());
  t.set("aaaabbbbccccdddXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
  // EXPECT_EQ("aaaabbbbccccddd", t.value());

  // Can set back to empty
  t.set("");
  EXPECT_EQ("", t.value());

  store_->shutdownThreading();
  tls_.shutdownThread();
}

class ThreadLocalStoreNoMocksTestBase : public testing::Test {
public:
  ThreadLocalStoreNoMocksTestBase()
      : alloc_(symbol_table_), store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)),
        pool_(symbol_table_) {}
  ~ThreadLocalStoreNoMocksTestBase() override {
    if (store_ != nullptr) {
      store_->shutdownThreading();
    }
  }

  StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  ThreadLocalStoreImplPtr store_;
  StatNamePool pool_;
};

class LookupWithStatNameTest : public ThreadLocalStoreNoMocksTestBase {};

TEST_F(LookupWithStatNameTest, All) {
  ScopePtr scope1 = store_->createScope("scope1.");
  Counter& c1 = store_->Store::counterFromStatName(makeStatName("c1"));
  Counter& c2 = scope1->counterFromStatName(makeStatName("c2"));
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = store_->Store::gaugeFromStatName(makeStatName("g1"), Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromStatName(makeStatName("g2"), Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g1.tags().size());

  Histogram& h1 =
      store_->Store::histogramFromStatName(makeStatName("h1"), Stats::Histogram::Unit::Unspecified);
  Histogram& h2 =
      scope1->histogramFromStatName(makeStatName("h2"), Stats::Histogram::Unit::Unspecified);
  scope1->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromStatName(makeStatName("bar")).name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopePtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());
}

TEST_F(LookupWithStatNameTest, NotFound) {
  StatName not_found(makeStatName("not_found"));
  EXPECT_FALSE(store_->findCounter(not_found));
  EXPECT_FALSE(store_->findGauge(not_found));
  EXPECT_FALSE(store_->findHistogram(not_found));
  EXPECT_FALSE(store_->findTextReadout(not_found));
}

class StatsMatcherTLSTest : public StatsThreadLocalStoreTest {
public:
  envoy::config::metrics::v3::StatsConfig stats_config_;
};

TEST_F(StatsMatcherTLSTest, TestNoOpStatImpls) {
  InSequence s;

  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "noop");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  // Testing No-op counters, gauges, histograms which match the prefix "noop".

  // Counter
  Counter& noop_counter = store_->counterFromString("noop_counter");
  EXPECT_EQ(noop_counter.name(), "");
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.add(1);
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.inc();
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.reset();
  EXPECT_EQ(noop_counter.value(), 0);
  Counter& noop_counter_2 = store_->counterFromString("noop_counter_2");
  EXPECT_EQ(&noop_counter, &noop_counter_2);
  EXPECT_FALSE(noop_counter.used());      // hardcoded to return false in NullMetricImpl.
  EXPECT_EQ(0, noop_counter.latch());     // hardcoded to 0.
  EXPECT_EQ(0, noop_counter.use_count()); // null counter is contained in ThreadLocalStoreImpl.

  // Gauge
  Gauge& noop_gauge = store_->gaugeFromString("noop_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(noop_gauge.name(), "");
  EXPECT_EQ(noop_gauge.value(), 0);
  noop_gauge.add(1);
  EXPECT_EQ(noop_gauge.value(), 0);
  noop_gauge.inc();
  EXPECT_EQ(noop_gauge.value(), 0);
  noop_gauge.dec();
  EXPECT_EQ(noop_gauge.value(), 0);
  noop_gauge.set(2);
  EXPECT_EQ(noop_gauge.value(), 0);
  noop_gauge.sub(2);
  EXPECT_EQ(noop_gauge.value(), 0);
  EXPECT_EQ(Gauge::ImportMode::NeverImport, noop_gauge.importMode());
  EXPECT_FALSE(noop_gauge.used());      // null gauge is contained in ThreadLocalStoreImpl.
  EXPECT_EQ(0, noop_gauge.use_count()); // null gauge is contained in ThreadLocalStoreImpl.

  Gauge& noop_gauge_2 = store_->gaugeFromString("noop_gauge_2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&noop_gauge, &noop_gauge_2);

  // TextReadout
  TextReadout& noop_string = store_->textReadoutFromString("noop_string");
  EXPECT_EQ(noop_string.name(), "");
  EXPECT_EQ("", noop_string.value());
  noop_string.set("hello");
  EXPECT_EQ("", noop_string.value());
  noop_string.set("hello");
  EXPECT_EQ("", noop_string.value());
  noop_string.set("goodbye");
  EXPECT_EQ("", noop_string.value());
  noop_string.set("hello");
  EXPECT_EQ("", noop_string.value());
  TextReadout& noop_string_2 = store_->textReadoutFromString("noop_string_2");
  EXPECT_EQ(&noop_string, &noop_string_2);

  // Histogram
  Histogram& noop_histogram =
      store_->histogramFromString("noop_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(noop_histogram.name(), "");
  EXPECT_FALSE(noop_histogram.used());
  EXPECT_EQ(Stats::Histogram::Unit::Null, noop_histogram.unit());
  Histogram& noop_histogram_2 =
      store_->histogramFromString("noop_histogram_2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(&noop_histogram, &noop_histogram_2);

  store_->shutdownThreading();
}

// We only test the exclusion list -- the inclusion list is the inverse, and both are tested in
// test/common/stats:stats_matcher_test.
TEST_F(StatsMatcherTLSTest, TestExclusionRegex) {
  InSequence s;

  // Expected to alloc lowercase_counter, lowercase_gauge, valid_counter, valid_gauge

  // Will block all stats containing any capital alphanumeric letter.
  stats_config_.mutable_stats_matcher()
      ->mutable_exclusion_list()
      ->add_patterns()
      ->set_hidden_envoy_deprecated_regex(".*[A-Z].*");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  // The creation of counters/gauges/histograms which have no uppercase letters should succeed.
  Counter& lowercase_counter = store_->counterFromString("lowercase_counter");
  EXPECT_EQ(lowercase_counter.name(), "lowercase_counter");
  Gauge& lowercase_gauge =
      store_->gaugeFromString("lowercase_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(lowercase_gauge.name(), "lowercase_gauge");
  Histogram& lowercase_histogram =
      store_->histogramFromString("lowercase_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(lowercase_histogram.name(), "lowercase_histogram");

  TextReadout& lowercase_string = store_->textReadoutFromString("lowercase_string");
  EXPECT_EQ(lowercase_string.name(), "lowercase_string");
  // And the creation of counters/gauges/histograms which have uppercase letters should fail.
  Counter& uppercase_counter = store_->counterFromString("UPPERCASE_counter");
  EXPECT_EQ(uppercase_counter.name(), "");
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);

  Gauge& uppercase_gauge =
      store_->gaugeFromString("uppercase_GAUGE", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(uppercase_gauge.name(), "");
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);

  TextReadout& uppercase_string = store_->textReadoutFromString("uppercase_STRING");
  EXPECT_EQ(uppercase_string.name(), "");
  uppercase_string.set("A STRING VALUE");
  EXPECT_EQ("", uppercase_string.value());

  // Histograms are harder to query and test, so we resort to testing that name() returns the empty
  // string.
  Histogram& uppercase_histogram =
      store_->histogramFromString("upperCASE_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(uppercase_histogram.name(), "");

  // Adding another exclusion rule -- now we reject not just uppercase stats but those starting with
  // the string "invalid".
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "invalid");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  Counter& valid_counter = store_->counterFromString("valid_counter");
  valid_counter.inc();
  EXPECT_EQ(valid_counter.value(), 1);

  Counter& invalid_counter = store_->counterFromString("invalid_counter");
  invalid_counter.inc();
  EXPECT_EQ(invalid_counter.value(), 0);

  // But the old exclusion rule still holds.
  Counter& invalid_counter_2 = store_->counterFromString("also_INVALID_counter");
  invalid_counter_2.inc();
  EXPECT_EQ(invalid_counter_2.value(), 0);

  // And we expect the same behavior from gauges and histograms.
  Gauge& valid_gauge = store_->gaugeFromString("valid_gauge", Gauge::ImportMode::Accumulate);
  valid_gauge.set(2);
  EXPECT_EQ(valid_gauge.value(), 2);

  Gauge& invalid_gauge_1 = store_->gaugeFromString("invalid_gauge", Gauge::ImportMode::Accumulate);
  invalid_gauge_1.inc();
  EXPECT_EQ(invalid_gauge_1.value(), 0);

  Gauge& invalid_gauge_2 =
      store_->gaugeFromString("also_INVALID_gauge", Gauge::ImportMode::Accumulate);
  invalid_gauge_2.inc();
  EXPECT_EQ(invalid_gauge_2.value(), 0);

  Histogram& valid_histogram =
      store_->histogramFromString("valid_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(valid_histogram.name(), "valid_histogram");

  Histogram& invalid_histogram_1 =
      store_->histogramFromString("invalid_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(invalid_histogram_1.name(), "");

  Histogram& invalid_histogram_2 =
      store_->histogramFromString("also_INVALID_histogram", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ(invalid_histogram_2.name(), "");

  TextReadout& valid_string = store_->textReadoutFromString("valid_string");
  valid_string.set("i'm valid");
  EXPECT_EQ("i'm valid", valid_string.value());

  TextReadout& invalid_string_1 = store_->textReadoutFromString("invalid_string");
  invalid_string_1.set("nope");
  EXPECT_EQ("", invalid_string_1.value());

  TextReadout& invalid_string_2 = store_->textReadoutFromString("also_INVLD_string");
  invalid_string_2.set("still no");
  EXPECT_EQ("", invalid_string_2.value());

  // Expected to free lowercase_counter, lowercase_gauge, valid_counter, valid_gauge
  store_->shutdownThreading();
}

// Tests the logic for caching the stats-matcher results, and in particular the
// private impl method checkAndRememberRejection(). That method behaves
// differently depending on whether TLS is enabled or not, so we parameterize
// the test accordingly; GetParam()==true means we want a TLS cache. In either
// case, we should never be calling the stats-matcher rejection logic more than
// once on given stat name.
class RememberStatsMatcherTest : public testing::TestWithParam<bool> {
public:
  RememberStatsMatcherTest()
      : heap_alloc_(symbol_table_), store_(heap_alloc_), scope_(store_.createScope("scope.")) {
    if (GetParam()) {
      store_.initializeThreading(main_thread_dispatcher_, tls_);
    }
  }

  ~RememberStatsMatcherTest() override {
    store_.shutdownThreading();
    tls_.shutdownThread();
  }

  using LookupStatFn = std::function<std::string(const std::string&)>;

  // Helper function to test the rejection cache. The goal here is to use
  // mocks to ensure that we don't call rejects() more than once on any of the
  // stats, even with 5 name-based lookups.
  void testRememberMatcher(const LookupStatFn lookup_stat) {
    InSequence s;

    MockStatsMatcher* matcher = new MockStatsMatcher;
    StatsMatcherPtr matcher_ptr(matcher);
    store_.setStatsMatcher(std::move(matcher_ptr));

    EXPECT_CALL(*matcher, rejects("scope.reject")).WillOnce(Return(true));
    EXPECT_CALL(*matcher, rejects("scope.ok")).WillOnce(Return(false));

    for (int j = 0; j < 5; ++j) {
      EXPECT_EQ("", lookup_stat("reject"));
      EXPECT_EQ("scope.ok", lookup_stat("ok"));
    }
  }

  void testRejectsAll(const LookupStatFn lookup_stat) {
    InSequence s;

    MockStatsMatcher* matcher = new MockStatsMatcher;
    matcher->rejects_all_ = true;
    StatsMatcherPtr matcher_ptr(matcher);
    store_.setStatsMatcher(std::move(matcher_ptr));

    ScopePtr scope = store_.createScope("scope.");

    for (int j = 0; j < 5; ++j) {
      // Note: zero calls to reject() are made, as reject-all should short-circuit.
      EXPECT_EQ("", lookup_stat("reject"));
    }
  }

  void testAcceptsAll(const LookupStatFn lookup_stat) {
    InSequence s;

    auto* matcher = new MockStatsMatcher;
    matcher->accepts_all_ = true;
    StatsMatcherPtr matcher_ptr(matcher);
    store_.setStatsMatcher(std::move(matcher_ptr));

    for (int j = 0; j < 5; ++j) {
      // Note: zero calls to reject() are made, as accept-all should short-circuit.
      EXPECT_EQ("scope.ok", lookup_stat("ok"));
    }
  }

  LookupStatFn lookupCounterFn() {
    return [this](const std::string& stat_name) -> std::string {
      return scope_->counterFromString(stat_name).name();
    };
  }

  LookupStatFn lookupGaugeFn() {
    return [this](const std::string& stat_name) -> std::string {
      return scope_->gaugeFromString(stat_name, Gauge::ImportMode::Accumulate).name();
    };
  }

// TODO(jmarantz): restore BoolIndicator tests when https://github.com/envoyproxy/envoy/pull/6280
// is reverted.
#define HAS_BOOL_INDICATOR 0
#if HAS_BOOL_INDICATOR
  LookupStatFn lookupBoolIndicator() {
    return [this](const std::string& stat_name) -> std::string {
      return scope_->boolIndicator(stat_name).name();
    };
  }
#endif

  LookupStatFn lookupHistogramFn() {
    return [this](const std::string& stat_name) -> std::string {
      return scope_->histogramFromString(stat_name, Stats::Histogram::Unit::Unspecified).name();
    };
  }

  LookupStatFn lookupTextReadoutFn() {
    return [this](const std::string& stat_name) -> std::string {
      return scope_->textReadoutFromString(stat_name).name();
    };
  }

  SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  AllocatorImpl heap_alloc_;
  ThreadLocalStoreImpl store_;
  ScopePtr scope_;
};

INSTANTIATE_TEST_SUITE_P(RememberStatsMatcherTest, RememberStatsMatcherTest,
                         testing::ValuesIn({false, true}));

// Tests that the logic for remembering rejected stats works properly, both
// with and without threading.
TEST_P(RememberStatsMatcherTest, CounterRejectOne) { testRememberMatcher(lookupCounterFn()); }

TEST_P(RememberStatsMatcherTest, CounterRejectsAll) { testRejectsAll(lookupCounterFn()); }

TEST_P(RememberStatsMatcherTest, CounterAcceptsAll) { testAcceptsAll(lookupCounterFn()); }

TEST_P(RememberStatsMatcherTest, GaugeRejectOne) { testRememberMatcher(lookupGaugeFn()); }

TEST_P(RememberStatsMatcherTest, GaugeRejectsAll) { testRejectsAll(lookupGaugeFn()); }

TEST_P(RememberStatsMatcherTest, GaugeAcceptsAll) { testAcceptsAll(lookupGaugeFn()); }

#if HAS_BOOL_INDICATOR
TEST_P(RememberStatsMatcherTest, BoolIndicatorRejectOne) {
  testRememberMatcher(lookupBoolIndicator());
}

TEST_P(RememberStatsMatcherTest, BoolIndicatorRejectsAll) { testRejectsAll(lookupBoolIndicator()); }

TEST_P(RememberStatsMatcherTest, BoolIndicatorAcceptsAll) { testAcceptsAll(lookupBoolIndicator()); }
#endif

TEST_P(RememberStatsMatcherTest, HistogramRejectOne) { testRememberMatcher(lookupHistogramFn()); }

TEST_P(RememberStatsMatcherTest, HistogramRejectsAll) { testRejectsAll(lookupHistogramFn()); }

TEST_P(RememberStatsMatcherTest, HistogramAcceptsAll) { testAcceptsAll(lookupHistogramFn()); }

TEST_P(RememberStatsMatcherTest, TextReadoutRejectOne) {
  testRememberMatcher(lookupTextReadoutFn());
}

TEST_P(RememberStatsMatcherTest, TextReadoutRejectsAll) { testRejectsAll(lookupTextReadoutFn()); }

TEST_P(RememberStatsMatcherTest, TextReadoutAcceptsAll) { testAcceptsAll(lookupTextReadoutFn()); }

TEST_F(StatsThreadLocalStoreTest, RemoveRejectedStats) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  Counter& counter = store_->counterFromString("c1");
  Gauge& gauge = store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Histogram& histogram = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  TextReadout& textReadout = store_->textReadoutFromString("t1");
  ASSERT_EQ(1, store_->counters().size()); // "c1".
  EXPECT_TRUE(&counter == store_->counters()[0].get() ||
              &counter == store_->counters()[1].get()); // counters() order is non-deterministic.
  ASSERT_EQ(1, store_->gauges().size());
  EXPECT_EQ("g1", store_->gauges()[0]->name());
  ASSERT_EQ(1, store_->histograms().size());
  EXPECT_EQ("h1", store_->histograms()[0]->name());
  ASSERT_EQ(1, store_->textReadouts().size());
  EXPECT_EQ("t1", store_->textReadouts()[0]->name());

  // Will effectively block all stats, and remove all the non-matching stats.
  envoy::config::metrics::v3::StatsConfig stats_config;
  stats_config.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_exact(
      "no-such-stat");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config));

  // They can no longer be found.
  EXPECT_EQ(0, store_->counters().size());
  EXPECT_EQ(0, store_->gauges().size());
  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, store_->textReadouts().size());

  // However, referencing the previously allocated stats will not crash.
  counter.inc();
  gauge.inc();
  EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), 42));
  histogram.recordValue(42);
  textReadout.set("fortytwo");
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, NonHotRestartNoTruncation) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Allocate a stat greater than the max name length.
  const std::string name_1(MaxStatNameLength + 1, 'A');

  store_->counterFromString(name_1);

  // This works fine, and we can find it by its long name because heap-stats do not
  // get truncated.
  EXPECT_NE(nullptr, TestUtility::findCounter(*store_, name_1).get());
  store_->shutdownThreading();
  tls_.shutdownThread();
}

class StatsThreadLocalStoreTestNoFixture : public testing::Test {
protected:
  StatsThreadLocalStoreTestNoFixture() : alloc_(symbol_table_), store_(alloc_) {
    store_.addSink(sink_);

    // Use a tag producer that will produce tags.
    envoy::config::metrics::v3::StatsConfig stats_config;
    store_.setTagProducer(std::make_unique<TagProducerImpl>(stats_config));
  }

  ~StatsThreadLocalStoreTestNoFixture() override {
    if (threading_enabled_) {
      store_.shutdownThreading();
      tls_.shutdownThread();
    }
  }

  void initThreading() {
    threading_enabled_ = true;
    store_.initializeThreading(main_thread_dispatcher_, tls_);
  }

  static constexpr size_t million_ = 1000 * 1000;

  MockSink sink_;
  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  ThreadLocalStoreImpl store_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  bool threading_enabled_{false};
};

// Tests how much memory is consumed allocating 100k stats.
TEST_F(StatsThreadLocalStoreTestNoFixture, MemoryWithoutTlsRealSymbolTable) {
  TestUtil::MemoryTest memory_test;
  TestUtil::forEachSampleStat(
      100, [this](absl::string_view name) { store_.counterFromString(std::string(name)); });
  EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 688080); // July 2, 2020
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 0.75 * million_);
}

TEST_F(StatsThreadLocalStoreTestNoFixture, MemoryWithTlsRealSymbolTable) {
  initThreading();
  TestUtil::MemoryTest memory_test;
  TestUtil::forEachSampleStat(
      100, [this](absl::string_view name) { store_.counterFromString(std::string(name)); });
  EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 827616); // Sep 25, 2020
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 0.9 * million_);
}

TEST_F(StatsThreadLocalStoreTest, ShuttingDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  store_->counterFromString("c1");
  store_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  store_->textReadoutFromString("t1");
  store_->shutdownThreading();
  store_->counterFromString("c2");
  store_->gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  store_->textReadoutFromString("t2");

  // We do not keep ref-counts for counters and gauges in the TLS cache, so
  // all these stats should have a ref-count of 2: one for the SharedPtr
  // returned from find*(), and one for the central cache.
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(2L, TestUtility::findGauge(*store_, "g1").use_count());

  // c1, g1, t1 should have a thread local ref, but c2, g2, t2 should not.
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(2L, TestUtility::findGauge(*store_, "g1").use_count());
  EXPECT_EQ(2L, TestUtility::findTextReadout(*store_, "t1").use_count());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c2").use_count());
  EXPECT_EQ(2L, TestUtility::findGauge(*store_, "g2").use_count());
  EXPECT_EQ(2L, TestUtility::findTextReadout(*store_, "t2").use_count());

  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, MergeDuringShutDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  store_->shutdownThreading();

  // Validate that merge callback is called during shutdown and there is no ASSERT.
  bool merge_called = false;
  store_->mergeHistograms([&merge_called]() -> void { merge_called = true; });

  EXPECT_TRUE(merge_called);
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST(ThreadLocalStoreThreadTest, ConstructDestruct) {
  SymbolTableImpl symbol_table;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
  NiceMock<ThreadLocal::MockInstance> tls;
  AllocatorImpl alloc(symbol_table);
  ThreadLocalStoreImpl store(alloc);

  store.initializeThreading(*dispatcher, tls);
  { ScopePtr scope1 = store.createScope("scope1."); }
  store.shutdownThreading();
}

// Histogram tests
TEST_F(HistogramTest, BasicSingleHistogramMerge) {
  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());

  expectCallAndAccumulate(h1, 0);
  expectCallAndAccumulate(h1, 43);
  expectCallAndAccumulate(h1, 41);
  expectCallAndAccumulate(h1, 415);
  expectCallAndAccumulate(h1, 2201);
  expectCallAndAccumulate(h1, 3201);
  expectCallAndAccumulate(h1, 125);
  expectCallAndAccumulate(h1, 13);

  EXPECT_EQ(1, validateMerge());
}

TEST_F(HistogramTest, BasicMultiHistogramMerge) {
  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  expectCallAndAccumulate(h1, 1);
  expectCallAndAccumulate(h2, 1);
  expectCallAndAccumulate(h2, 2);

  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, MultiHistogramMultipleMerges) {
  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  // Insert one value in to one histogram and validate
  expectCallAndAccumulate(h1, 1);
  EXPECT_EQ(2, validateMerge());

  // Insert value into second histogram and validate that it is merged properly.
  expectCallAndAccumulate(h2, 1);
  EXPECT_EQ(2, validateMerge());

  // Insert more values into both the histograms and validate that it is merged properly.
  expectCallAndAccumulate(h1, 2);
  EXPECT_EQ(2, validateMerge());

  expectCallAndAccumulate(h2, 3);
  EXPECT_EQ(2, validateMerge());

  expectCallAndAccumulate(h2, 2);
  EXPECT_EQ(2, validateMerge());

  // Do not insert any value and validate that intervalSummary is empty for both the histograms and
  // cumulativeSummary has right values.
  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, BasicScopeHistogramMerge) {
  ScopePtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());

  expectCallAndAccumulate(h1, 2);
  expectCallAndAccumulate(h2, 2);
  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, BasicHistogramSummaryValidate) {
  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  expectCallAndAccumulate(h1, 1);

  EXPECT_EQ(2, validateMerge());

  const std::string h1_expected_summary =
      "P0: 1, P25: 1.025, P50: 1.05, P75: 1.075, P90: 1.09, P95: 1.095, "
      "P99: 1.099, P99.5: 1.0995, P99.9: 1.0999, P100: 1.1";
  const std::string h2_expected_summary =
      "P0: 0, P25: 25, P50: 50, P75: 75, P90: 90, P95: 95, P99: 99, "
      "P99.5: 99.5, P99.9: 99.9, P100: 100";

  const std::string h1_expected_buckets =
      "B0.5: 0, B1: 0, B5: 1, B10: 1, B25: 1, B50: 1, B100: 1, B250: 1, "
      "B500: 1, B1000: 1, B2500: 1, B5000: 1, B10000: 1, B30000: 1, B60000: 1, "
      "B300000: 1, B600000: 1, B1.8e+06: 1, B3.6e+06: 1";
  const std::string h2_expected_buckets =
      "B0.5: 1, B1: 1, B5: 5, B10: 10, B25: 25, B50: 50, B100: 100, B250: 100, "
      "B500: 100, B1000: 100, B2500: 100, B5000: 100, B10000: 100, B30000: 100, "
      "B60000: 100, B300000: 100, B600000: 100, B1.8e+06: 100, B3.6e+06: 100";

  for (size_t i = 0; i < 100; ++i) {
    expectCallAndAccumulate(h2, i);
  }

  EXPECT_EQ(2, validateMerge());

  NameHistogramMap name_histogram_map = makeHistogramMap(store_->histograms());
  EXPECT_EQ(h1_expected_summary,
            name_histogram_map["h1"]->cumulativeStatistics().quantileSummary());
  EXPECT_EQ(h2_expected_summary,
            name_histogram_map["h2"]->cumulativeStatistics().quantileSummary());
  EXPECT_EQ(h1_expected_buckets, name_histogram_map["h1"]->cumulativeStatistics().bucketSummary());
  EXPECT_EQ(h2_expected_buckets, name_histogram_map["h2"]->cumulativeStatistics().bucketSummary());
}

// Validates the summary after known value merge in to same histogram.
TEST_F(HistogramTest, BasicHistogramMergeSummary) {
  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  for (size_t i = 0; i < 50; ++i) {
    expectCallAndAccumulate(h1, i);
  }
  EXPECT_EQ(1, validateMerge());

  for (size_t i = 50; i < 100; ++i) {
    expectCallAndAccumulate(h1, i);
  }
  EXPECT_EQ(1, validateMerge());

  const std::string expected_summary = "P0: 0, P25: 25, P50: 50, P75: 75, P90: 90, P95: 95, P99: "
                                       "99, P99.5: 99.5, P99.9: 99.9, P100: 100";
  const std::string expected_bucket_summary =
      "B0.5: 1, B1: 1, B5: 5, B10: 10, B25: 25, B50: 50, B100: 100, B250: 100, "
      "B500: 100, B1000: 100, B2500: 100, B5000: 100, B10000: 100, B30000: 100, "
      "B60000: 100, B300000: 100, B600000: 100, B1.8e+06: 100, B3.6e+06: 100";

  NameHistogramMap name_histogram_map = makeHistogramMap(store_->histograms());
  EXPECT_EQ(expected_summary, name_histogram_map["h1"]->cumulativeStatistics().quantileSummary());
  EXPECT_EQ(expected_bucket_summary,
            name_histogram_map["h1"]->cumulativeStatistics().bucketSummary());
}

TEST_F(HistogramTest, BasicHistogramUsed) {
  ScopePtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  NameHistogramMap name_histogram_map = makeHistogramMap(store_->histograms());
  EXPECT_FALSE(name_histogram_map["h1"]->used());
  EXPECT_FALSE(name_histogram_map["h2"]->used());

  // Merge the histograms and validate that h1 is considered used.
  store_->mergeHistograms([]() -> void {});
  EXPECT_TRUE(name_histogram_map["h1"]->used());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 2));
  h2.recordValue(2);
  EXPECT_FALSE(name_histogram_map["h2"]->used());

  // Merge histograms again and validate that both h1 and h2 are used.
  store_->mergeHistograms([]() -> void {});

  for (const ParentHistogramSharedPtr& histogram : store_->histograms()) {
    EXPECT_TRUE(histogram->used());
  }
}

TEST_F(HistogramTest, ParentHistogramBucketSummary) {
  ScopePtr scope1 = store_->createScope("scope1.");
  Histogram& histogram =
      store_->histogramFromString("histogram", Stats::Histogram::Unit::Unspecified);
  store_->mergeHistograms([]() -> void {});
  ASSERT_EQ(1, store_->histograms().size());
  ParentHistogramSharedPtr parent_histogram = store_->histograms()[0];
  EXPECT_EQ("No recorded values", parent_histogram->bucketSummary());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), 10));
  histogram.recordValue(10);
  store_->mergeHistograms([]() -> void {});
  EXPECT_EQ("B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(1,1) B50(1,1) B100(1,1) "
            "B250(1,1) B500(1,1) B1000(1,1) B2500(1,1) B5000(1,1) B10000(1,1) "
            "B30000(1,1) B60000(1,1) B300000(1,1) B600000(1,1) B1.8e+06(1,1) "
            "B3.6e+06(1,1)",
            parent_histogram->bucketSummary());
}

class ThreadLocalRealThreadsTestBase : public ThreadLocalStoreNoMocksTestBase {
protected:
  static constexpr uint32_t NumScopes = 1000;
  static constexpr uint32_t NumIters = 35;

  // Helper class to block on a number of multi-threaded operations occurring.
  class BlockingBarrier {
  public:
    explicit BlockingBarrier(uint32_t count) : blocking_counter_(count) {}
    ~BlockingBarrier() { blocking_counter_.Wait(); }

    /**
     * Returns a function that first executes 'f', and then decrements the count
     * toward unblocking the scope. This is intended to be used as a post() callback.
     *
     * @param f the function to run prior to decrementing the count.
     */
    std::function<void()> run(std::function<void()> f) {
      return [this, f]() {
        f();
        decrementCount();
      };
    }

    /**
     * @return a function that, when run, decrements the count, intended for passing to post().
     */
    std::function<void()> decrementCountFn() {
      return [this] { decrementCount(); };
    }

    void decrementCount() { blocking_counter_.DecrementCount(); }

  private:
    absl::BlockingCounter blocking_counter_;
  };

  ThreadLocalRealThreadsTestBase(uint32_t num_threads)
      : num_threads_(num_threads), start_time_(time_system_.monotonicTime()),
        api_(Api::createApiForTest()), thread_factory_(api_->threadFactory()),
        pool_(store_->symbolTable()) {
    // This is the same order as InstanceImpl::initialize in source/server/server.cc.
    thread_dispatchers_.resize(num_threads_);
    {
      BlockingBarrier blocking_barrier(num_threads_ + 1);
      main_thread_ = thread_factory_.createThread(
          [this, &blocking_barrier]() { mainThreadFn(blocking_barrier); });
      for (uint32_t i = 0; i < num_threads_; ++i) {
        threads_.emplace_back(thread_factory_.createThread(
            [this, i, &blocking_barrier]() { workerThreadFn(i, blocking_barrier); }));
      }
    }

    {
      BlockingBarrier blocking_barrier(1);
      main_dispatcher_->post(blocking_barrier.run([this]() {
        tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
        tls_->registerThread(*main_dispatcher_, true);
        for (Event::DispatcherPtr& dispatcher : thread_dispatchers_) {
          // Worker threads must be registered from the main thread, per assert in registerThread().
          tls_->registerThread(*dispatcher, false);
        }
        store_->initializeThreading(*main_dispatcher_, *tls_);
      }));
    }
  }

  ~ThreadLocalRealThreadsTestBase() override {
    {
      BlockingBarrier blocking_barrier(1);
      main_dispatcher_->post(blocking_barrier.run([this]() {
        store_->shutdownThreading();
        tls_->shutdownGlobalThreading();
        tls_->shutdownThread();
      }));
    }

    for (Event::DispatcherPtr& dispatcher : thread_dispatchers_) {
      dispatcher->post([&dispatcher]() { dispatcher->exit(); });
    }

    for (Thread::ThreadPtr& thread : threads_) {
      thread->join();
    }

    main_dispatcher_->post([this]() {
      store_.reset();
      tls_.reset();
      main_dispatcher_->exit();
    });
    main_thread_->join();
  }

  void workerThreadFn(uint32_t thread_index, BlockingBarrier& blocking_barrier) {
    thread_dispatchers_[thread_index] =
        api_->allocateDispatcher(absl::StrCat("test_worker_", thread_index));
    blocking_barrier.decrementCount();
    thread_dispatchers_[thread_index]->run(Event::Dispatcher::RunType::RunUntilExit);
  }

  void mainThreadFn(BlockingBarrier& blocking_barrier) {
    main_dispatcher_ = api_->allocateDispatcher("test_main_thread");
    blocking_barrier.decrementCount();
    main_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  }

  void mainDispatchBlock() {
    // To ensure all stats are freed we have to wait for a few posts() to clear.
    // First, wait for the main-dispatcher to initiate the cross-thread TLS cleanup.
    BlockingBarrier blocking_barrier(1);
    main_dispatcher_->post(blocking_barrier.run([]() {}));
  }

  void tlsBlock() {
    BlockingBarrier blocking_barrier(num_threads_);
    for (Event::DispatcherPtr& thread_dispatcher : thread_dispatchers_) {
      thread_dispatcher->post(blocking_barrier.run([]() {}));
    }
  }

  const uint32_t num_threads_;
  Event::TestRealTimeSystem time_system_;
  MonotonicTime start_time_;
  Api::ApiPtr api_;
  Event::DispatcherPtr main_dispatcher_;
  std::vector<Event::DispatcherPtr> thread_dispatchers_;
  Thread::ThreadFactory& thread_factory_;
  ThreadLocal::InstanceImplPtr tls_;
  Thread::ThreadPtr main_thread_;
  std::vector<Thread::ThreadPtr> threads_;
  StatNamePool pool_;
};

class ClusterShutdownCleanupStarvationTest : public ThreadLocalRealThreadsTestBase {
protected:
  static constexpr uint32_t NumThreads = 2;

  ClusterShutdownCleanupStarvationTest()
      : ThreadLocalRealThreadsTestBase(NumThreads), my_counter_name_(pool_.add("my_counter")),
        my_counter_scoped_name_(pool_.add("scope.my_counter")) {}

  void createScopesIncCountersAndCleanup() {
    for (uint32_t i = 0; i < NumScopes; ++i) {
      ScopePtr scope = store_->createScope("scope.");
      Counter& counter = scope->counterFromStatName(my_counter_name_);
      counter.inc();
    }
  }

  void createScopesIncCountersAndCleanupAllThreads() {
    BlockingBarrier blocking_barrier(NumThreads);
    for (Event::DispatcherPtr& thread_dispatcher : thread_dispatchers_) {
      thread_dispatcher->post(
          blocking_barrier.run([this]() { createScopesIncCountersAndCleanup(); }));
    }
  }

  std::chrono::seconds elapsedTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(time_system_.monotonicTime() -
                                                            start_time_);
  }

  StatName my_counter_name_;
  StatName my_counter_scoped_name_;
};

// Tests the scenario where a cluster and stat are allocated in multiple
// concurrent threads, but after each round of allocation/free we post() an
// empty callback to main to ensure that cross-scope thread cleanups complete.
// In this test, we don't expect the use-count of the stat to get very high.
TEST_F(ClusterShutdownCleanupStarvationTest, TwelveThreadsWithBlockade) {
  for (uint32_t i = 0; i < NumIters && elapsedTime() < std::chrono::seconds(5); ++i) {
    createScopesIncCountersAndCleanupAllThreads();

    // First, wait for the main-dispatcher to initiate the cross-thread TLS cleanup.
    mainDispatchBlock();

    // Next, wait for all the worker threads to complete their TLS cleanup.
    tlsBlock();

    // Finally, wait for the final central-cache cleanup, which occurs on the main thread.
    mainDispatchBlock();

    // Here we show that the counter cleanups have finished, because the use-count is 1.
    CounterSharedPtr counter =
        alloc_.makeCounter(my_counter_scoped_name_, StatName(), StatNameTagVector{});
    EXPECT_EQ(1, counter->use_count()) << "index=" << i;
  }
}

// In this test, we don't run the main-callback post() in between each
// iteration, and we use a thread synchronizer to block the cross-thread
// cleanup. Thus no stat references in the caches get freed and the use_count()
// grows without bound.
TEST_F(ClusterShutdownCleanupStarvationTest, TwelveThreadsWithoutBlockade) {
  store_->sync().enable();
  store_->sync().waitOn(ThreadLocalStoreImpl::MainDispatcherCleanupSync);
  for (uint32_t i = 0; i < NumIters && elapsedTime() < std::chrono::seconds(5); ++i) {
    createScopesIncCountersAndCleanupAllThreads();
    // As we have blocked the main dispatcher cleanup function above, nothing
    // gets cleaned up and use-counts grow without bound as we recreate scopes,
    // recreating the same counter in each one.

    // Compute the use-count of one of the counters. This shows that by blocking
    // the main-dispatcher cleanup thread, the use-counts grow without bound.
    // We set our parameters so we attempt to exceed a use-count of 64k when
    // running the test: NumScopes*NumThreads*NumIters == 70000, We use a timer
    // so we don't time out on asan/tsan tests, In opt builds this test takes
    // less than a second, and in fastbuild it takes less than 5.
    CounterSharedPtr counter =
        alloc_.makeCounter(my_counter_scoped_name_, StatName(), StatNameTagVector{});
    uint32_t use_count = counter->use_count() - 1; // Subtract off this instance.
    EXPECT_EQ((i + 1) * NumScopes * NumThreads, use_count);
  }
  EXPECT_EQ(70000, NumThreads * NumScopes * NumIters);
  store_->sync().signal(ThreadLocalStoreImpl::MainDispatcherCleanupSync);
}

class HistogramThreadTest : public ThreadLocalRealThreadsTestBase {
protected:
  static constexpr uint32_t NumThreads = 10;

  HistogramThreadTest() : ThreadLocalRealThreadsTestBase(NumThreads) {}

  void mergeHistograms() {
    BlockingBarrier blocking_barrier(1);
    main_dispatcher_->post([this, &blocking_barrier]() {
      store_->mergeHistograms(blocking_barrier.decrementCountFn());
    });
  }

  uint32_t numTlsHistograms() {
    uint32_t num;
    {
      BlockingBarrier blocking_barrier(1);
      main_dispatcher_->post([this, &num, &blocking_barrier]() {
        ThreadLocalStoreTestingPeer::numTlsHistograms(*store_,
                                                      [&num, &blocking_barrier](uint32_t num_hist) {
                                                        num = num_hist;
                                                        blocking_barrier.decrementCount();
                                                      });
      });
    }
    return num;
  }

  // Executes a function on every worker thread dispatcher.
  void foreachThread(const std::function<void()>& fn) {
    BlockingBarrier blocking_barrier(NumThreads);
    for (Event::DispatcherPtr& thread_dispatcher : thread_dispatchers_) {
      thread_dispatcher->post(blocking_barrier.run(fn));
    }
  }
};

TEST_F(HistogramThreadTest, MakeHistogramsAndRecordValues) {
  foreachThread([this]() {
    Histogram& histogram =
        store_->histogramFromString("my_hist", Stats::Histogram::Unit::Unspecified);
    histogram.recordValue(42);
  });

  mergeHistograms();

  auto histograms = store_->histograms();
  ASSERT_EQ(1, histograms.size());
  ParentHistogramSharedPtr hist = histograms[0];
  EXPECT_THAT(hist->bucketSummary(),
              HasSubstr(absl::StrCat(" B25(0,0) B50(", NumThreads, ",", NumThreads, ") ")));
}

TEST_F(HistogramThreadTest, ScopeOverlap) {
  // Creating two scopes with the same name gets you two distinct scope objects.
  ScopePtr scope1 = store_->createScope("scope.");
  ScopePtr scope2 = store_->createScope("scope.");
  EXPECT_NE(scope1, scope2);

  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, numTlsHistograms());

  // Histograms created in the two same-named scopes will be the same objects.
  foreachThread([&scope1, &scope2]() {
    Histogram& histogram = scope1->histogramFromString("histogram", Histogram::Unit::Unspecified);
    EXPECT_EQ(&histogram, &scope2->histogramFromString("histogram", Histogram::Unit::Unspecified));
    histogram.recordValue(100);
  });

  mergeHistograms();

  // Verify that we have the expected number of TLS histograms since we accessed
  // the histogram on every thread.
  std::vector<ParentHistogramSharedPtr> histograms = store_->histograms();
  ASSERT_EQ(1, histograms.size());
  EXPECT_EQ(NumThreads, numTlsHistograms());

  // There's no convenient API to pull data out of the histogram, except as
  // a string. This expectation captures the bucket transition to indicate
  // 0 samples at less than 100, and 10 between 100 and 249 inclusive.
  EXPECT_THAT(histograms[0]->bucketSummary(),
              HasSubstr(absl::StrCat(" B100(0,0) B250(", NumThreads, ",", NumThreads, ") ")));

  // The histogram was created in scope1, which can now be destroyed. But the
  // histogram is kept alive by scope2.
  scope1.reset();
  histograms = store_->histograms();
  EXPECT_EQ(1, histograms.size());
  EXPECT_EQ(NumThreads, numTlsHistograms());

  // We can continue to accumulate samples at the scope2's view of the same
  // histogram, and they will combine with the existing data, despite the
  // fact that scope1 has been deleted.
  foreachThread([&scope2]() {
    Histogram& histogram = scope2->histogramFromString("histogram", Histogram::Unit::Unspecified);
    histogram.recordValue(300);
  });

  mergeHistograms();

  // Shows the bucket summary with 10 samples at >=100, and 20 at >=250.
  EXPECT_THAT(histograms[0]->bucketSummary(),
              HasSubstr(absl::StrCat(" B100(0,0) B250(0,", NumThreads, ") B500(", NumThreads, ",",
                                     2 * NumThreads, ") ")));

  // Now clear everything, and synchronize the system by calling mergeHistograms().
  // THere should be no more ParentHistograms or TlsHistograms.
  scope2.reset();
  histograms.clear();
  mergeHistograms();

  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, numTlsHistograms());

  store_->shutdownThreading();

  store_->histogramFromString("histogram_after_shutdown", Histogram::Unit::Unspecified);
}

} // namespace Stats
} // namespace Envoy
