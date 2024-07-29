#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/memory/stats.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/common/stats/real_thread_test_base.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

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
    thread_local_store_impl.tls_cache_->runOnAllThreads(
        [num_tls_histograms](OptRef<ThreadLocalStoreImpl::TlsCache> tls_cache) {
          *num_tls_histograms += tls_cache->tls_histogram_cache_.size();
        },
        [num_tls_hist_cb, num_tls_histograms]() { num_tls_hist_cb(*num_tls_histograms); });
  }
};

class StatsThreadLocalStoreTest : public testing::Test {
public:
  StatsThreadLocalStoreTest()
      : alloc_(symbol_table_), store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)),
        scope_(*store_->rootScope()) {
    store_->addSink(sink_);
  }

  ~StatsThreadLocalStoreTest() override {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
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

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  AllocatorImpl alloc_;
  MockSink sink_;
  ThreadLocalStoreImplPtr store_;
  Scope& scope_;
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
  using Bucket = ParentHistogram::Bucket;
  using NameHistogramMap = std::map<std::string, ParentHistogramSharedPtr>;

  HistogramTest()
      : pool_(symbol_table_), alloc_(symbol_table_),
        store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)), scope_(*store_->rootScope()) {
    store_->addSink(sink_);
    store_->initializeThreading(main_thread_dispatcher_, tls_);
  }

  ~HistogramTest() override {
    tls_.shutdownGlobalThreading();
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

  TestUtil::TestSinkPredicates& testSinkPredicatesOrDie() {
    auto predicates = dynamic_cast<TestUtil::TestSinkPredicates*>(store_->sinkPredicates().ptr());
    ASSERT(predicates != nullptr);
    return *predicates;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  StatNamePool pool_;
  AllocatorImpl alloc_;
  MockSink sink_;
  ThreadLocalStoreImplPtr store_;
  Scope& scope_;
  InSequence s;
  std::vector<uint64_t> h1_cumulative_values_, h2_cumulative_values_, h1_interval_values_,
      h2_interval_values_;
};

TEST_F(StatsThreadLocalStoreTest, NoTls) {
  InSequence s;

  Counter& c1 = scope_.counterFromString("c1");
  EXPECT_EQ(&c1, &scope_.counterFromString("c1"));
  StatNameManagedStorage c1_name("c1", symbol_table_);
  c1.add(100);

  auto found_counter = scope_.findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&g1, &scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate));
  StatNameManagedStorage g1_name("g1", symbol_table_);
  g1.set(100);

  auto found_gauge = scope_.findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  EXPECT_EQ(&h1, &scope_.histogramFromString("h1", Histogram::Unit::Unspecified));
  StatNameManagedStorage h1_name("h1", symbol_table_);

  auto found_histogram = scope_.findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());
  TextReadout& t1 = scope_.textReadoutFromString("t1");
  EXPECT_EQ(&t1, &scope_.textReadoutFromString("t1"));

  auto found_text_readout = scope_.findTextReadout(t1.statName());
  ASSERT_TRUE(found_text_readout.has_value());
  EXPECT_EQ(&t1, &found_text_readout->get());
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

  Counter& c1 = scope_.counterFromString("c1");
  EXPECT_EQ(&c1, &scope_.counterFromString("c1"));
  StatNameManagedStorage c1_name("c1", symbol_table_);
  c1.add(100);
  auto found_counter = scope_.findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&g1, &scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate));
  StatNameManagedStorage g1_name("g1", symbol_table_);
  g1.set(100);
  auto found_gauge = scope_.findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  EXPECT_EQ(&h1, &scope_.histogramFromString("h1", Histogram::Unit::Unspecified));
  StatNameManagedStorage h1_name("h1", symbol_table_);
  auto found_histogram = scope_.findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());

  TextReadout& t1 = scope_.textReadoutFromString("t1");
  EXPECT_EQ(&t1, &scope_.textReadoutFromString("t1"));

  EXPECT_EQ(1UL, store_->counters().size());

  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());
  EXPECT_EQ(1UL, store_->textReadouts().size());
  EXPECT_EQ(&t1, store_->textReadouts().front().get()); // front() ok when size()==1
  EXPECT_EQ(2UL, store_->textReadouts().front().use_count());

  tls_.shutdownGlobalThreading();
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

  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  Counter& c1 = scope_.counterFromString("c1");
  Counter& c2 = scope1->counterFromString("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  StatNameManagedStorage c1_name("c1", symbol_table_);
  auto found_counter = scope_.findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  StatNameManagedStorage c2_name("scope1.c2", symbol_table_);
  auto found_counter2 = scope1->findCounter(c2_name.statName());
  ASSERT_TRUE(found_counter2.has_value());
  EXPECT_EQ(&c2, &found_counter2->get());

  Gauge& g1 = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  StatNameManagedStorage g1_name("g1", symbol_table_);
  auto found_gauge = scope_.findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  StatNameManagedStorage g2_name("scope1.g2", symbol_table_);
  auto found_gauge2 = scope1->findGauge(g2_name.statName());
  ASSERT_TRUE(found_gauge2.has_value());
  EXPECT_EQ(&g2, &found_gauge2->get());

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 200));
  h2.recordValue(200);
  StatNameManagedStorage h1_name("h1", symbol_table_);
  auto found_histogram = scope_.findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());
  StatNameManagedStorage h2_name("scope1.h2", symbol_table_);
  auto found_histogram2 = scope1->findHistogram(h2_name.statName());
  ASSERT_TRUE(found_histogram2.has_value());
  EXPECT_EQ(&h2, &found_histogram2->get());

  TextReadout& t1 = scope_.textReadoutFromString("t1");
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
    Histogram& histogram = scope1->histogramFromStatNameWithTags(StatName(storage.statName()), tags,
                                                                 Histogram::Unit::Unspecified);
    EXPECT_EQ(expectedTags, histogram.tags());
    EXPECT_EQ(&histogram, &scope1->histogramFromStatNameWithTags(StatName(storage.statName()), tags,
                                                                 Histogram::Unit::Unspecified));
  }

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  store_->deliverHistogramToSinks(h1, 100);
  store_->deliverHistogramToSinks(h2, 200);
  scope1.reset();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, HistogramScopeOverlap) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Creating two scopes with the same name gets you two distinct scope objects.
  ScopeSharedPtr scope1 = store_->createScope("scope.");
  ScopeSharedPtr scope2 = store_->createScope("scope.");
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

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();

  scope_.histogramFromString("histogram_after_shutdown", Histogram::Unit::Unspecified);

  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, ForEach) {
  auto collect_scopes = [this]() -> std::vector<std::string> {
    std::vector<std::string> names;
    store_->forEachScope([](size_t) {},
                         [&names](const Scope& scope) {
                           names.push_back(scope.constSymbolTable().toString(scope.prefix()));
                         });
    return names;
  };
  auto collect_counters = [this]() -> std::vector<std::string> {
    std::vector<std::string> names;
    store_->forEachCounter([](size_t) {},
                           [&names](Counter& counter) { names.push_back(counter.name()); });
    return names;
  };
  auto collect_gauges = [this]() -> std::vector<std::string> {
    std::vector<std::string> names;
    store_->forEachGauge([](size_t) {}, [&names](Gauge& gauge) { names.push_back(gauge.name()); });
    return names;
  };
  auto collect_text_readouts = [this]() -> std::vector<std::string> {
    std::vector<std::string> names;
    store_->forEachTextReadout(
        [](size_t) {},
        [&names](TextReadout& text_readout) { names.push_back(text_readout.name()); });
    return names;
  };

  // TODO(pradeepcrao): add tests for histograms when forEachHistogram is added

  const std::vector<std::string> empty;

  EXPECT_THAT(collect_scopes(), UnorderedElementsAreArray({""}));
  EXPECT_THAT(collect_counters(), UnorderedElementsAreArray(empty));
  EXPECT_THAT(collect_gauges(), UnorderedElementsAreArray(empty));
  EXPECT_THAT(collect_text_readouts(), UnorderedElementsAreArray(empty));

  ScopeSharedPtr scope1 = store_->createScope("scope1");
  scope1->counterFromString("counter1");
  scope1->gaugeFromString("gauge1", Gauge::ImportMode::Accumulate);
  scope1->textReadoutFromString("tr1");
  ScopeSharedPtr scope2 = scope1->createScope("scope2");
  scope2->counterFromString("counter2");
  scope2->gaugeFromString("gauge2", Gauge::ImportMode::Accumulate);
  scope2->textReadoutFromString("tr2");
  ScopeSharedPtr scope3 = store_->createScope("scope3");
  EXPECT_THAT(collect_scopes(),
              UnorderedElementsAreArray({"", "scope1", "scope1.scope2", "scope3"}));
  EXPECT_THAT(collect_counters(),
              UnorderedElementsAreArray({"scope1.counter1", "scope1.scope2.counter2"}));
  EXPECT_THAT(collect_gauges(),
              UnorderedElementsAreArray({"scope1.gauge1", "scope1.scope2.gauge2"}));
  EXPECT_THAT(collect_text_readouts(),
              UnorderedElementsAreArray({"scope1.tr1", "scope1.scope2.tr2"}));
}

// Validate that we sanitize away bad characters in the stats prefix.
TEST_F(StatsThreadLocalStoreTest, SanitizePrefix) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopeSharedPtr scope1 = store_->createScope(std::string("scope1:\0:foo.", 13));
  Counter& c1 = scope1->counterFromString("c1");
  EXPECT_EQ("scope1___foo.c1", c1.name());

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, ConstSymtabAccessor) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  const Scope& cscope = *scope;
  const SymbolTable& const_symbol_table = cscope.constSymbolTable();
  SymbolTable& symbol_table = scope->symbolTable();
  EXPECT_EQ(&const_symbol_table, &symbol_table);
}

TEST_F(StatsThreadLocalStoreTest, ScopeDelete) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  scope1->counterFromString("c1");
  EXPECT_EQ(1UL, store_->counters().size());
  CounterSharedPtr c1 = TestUtility::findCounter(*store_, "scope1.c1");
  EXPECT_EQ("scope1.c1", c1->name());

  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(tls_, runOnAllThreads(_, _)).Times(testing::AtLeast(1));
  scope1.reset();
  // The counter is gone from all scopes, but is still held in the local
  // variable c1. Hence, it will not be removed from the allocator or store.
  EXPECT_EQ(1UL, store_->counters().size());

  EXPECT_EQ(1L, c1.use_count());
  c1.reset();
  // Removing the counter from the local variable, should now remove it from the
  // allocator.
  EXPECT_EQ(0UL, store_->counters().size());

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, NestedScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  Counter& c1 = scope1->counterFromString("foo.bar");
  EXPECT_EQ("scope1.foo.bar", c1.name());
  StatNameManagedStorage c1_name("scope1.foo.bar", symbol_table_);
  auto found_counter = scope1->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());

  ScopeSharedPtr scope2 = scope1->createScope("foo.");
  Counter& c2 = scope2->counterFromString("bar");
  EXPECT_EQ(&c1, &c2);
  EXPECT_EQ("scope1.foo.bar", c2.name());
  StatNameManagedStorage c2_name("scope1.foo.bar", symbol_table_);
  auto found_counter2 = scope2->findCounter(c2_name.statName());
  ASSERT_TRUE(found_counter2.has_value());

  // Different allocations point to the same referenced counted backing memory.
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(c1.value(), c2.value());

  Gauge& g1 = scope2->gaugeFromString("some_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope1.foo.some_gauge", g1.name());

  TextReadout& t1 = scope2->textReadoutFromString("some_string");
  EXPECT_EQ("scope1.foo.some_string", t1.name());

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, OverlappingScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Both scopes point to the same namespace. This can happen during reload of a cluster for
  // example.
  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  ScopeSharedPtr scope2 = store_->createScope("scope1.");

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

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, TextReadoutAllLengths) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  TextReadout& t = scope_.textReadoutFromString("t");
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

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, SharedScopes) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  std::vector<ConstScopeSharedPtr> scopes;

  // Verifies shared_ptr functionality by creating some scopes, iterating
  // through them from the store and saving them in a vector, dropping the
  // references, and then referencing the scopes, verifying their names.
  {
    ScopeSharedPtr scope1 = store_->createScope("scope1.");
    ScopeSharedPtr scope2 = store_->createScope("scope2.");
    store_->forEachScope(
        [](size_t) {}, [&scopes](const Scope& scope) { scopes.push_back(scope.getConstShared()); });
  }
  ASSERT_EQ(3,
            scopes.size()); // For some reason there are two scopes created with name "" by default.
  store_->symbolTable().sortByStatNames<ConstScopeSharedPtr>(
      scopes.begin(), scopes.end(),
      [](const ConstScopeSharedPtr& scope) -> StatName { return scope->prefix(); });
  EXPECT_EQ("", store_->symbolTable().toString(scopes[0]->prefix())); // default scope
  EXPECT_EQ("scope1", store_->symbolTable().toString(scopes[1]->prefix()));
  EXPECT_EQ("scope2", store_->symbolTable().toString(scopes[2]->prefix()));

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, ExtractAndAppendTagsFixedValue) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  envoy::config::metrics::v3::StatsConfig stats_config;
  auto* tag_specifier = stats_config.add_stats_tags();
  tag_specifier->set_tag_name("foo");
  tag_specifier->set_fixed_value("bar");

  const Stats::TagVector tags_vector;
  store_->setTagProducer(TagProducerImpl::createTagProducer(stats_config, tags_vector).value());

  StatNamePool pool(symbol_table_);
  StatNameTagVector tags{{pool.add("a"), pool.add("b")}};
  store_->extractAndAppendTags(pool.add("c1"), pool, tags);

  ASSERT_EQ(2, tags.size());
  EXPECT_EQ("a", symbol_table_.toString(tags[0].first));
  EXPECT_EQ("b", symbol_table_.toString(tags[0].second));
  EXPECT_EQ("foo", symbol_table_.toString(tags[1].first));
  EXPECT_EQ("bar", symbol_table_.toString(tags[1].second));
  EXPECT_THAT(store_->fixedTags(), UnorderedElementsAre(Tag{"foo", "bar"}));
}

TEST_F(StatsThreadLocalStoreTest, ExtractAndAppendTagsRegexValueNoMatch) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  envoy::config::metrics::v3::StatsConfig stats_config;
  auto* tag_specifier = stats_config.add_stats_tags();
  tag_specifier->set_tag_name("foo");
  tag_specifier->set_regex("bar");

  const Stats::TagVector tags_vector;
  store_->setTagProducer(TagProducerImpl::createTagProducer(stats_config, tags_vector).value());

  StatNamePool pool(symbol_table_);
  StatNameTagVector tags{{pool.add("a"), pool.add("b")}};
  store_->extractAndAppendTags(pool.add("c1"), pool, tags);

  ASSERT_EQ(1, tags.size());
  EXPECT_EQ("a", symbol_table_.toString(tags[0].first));
  EXPECT_EQ("b", symbol_table_.toString(tags[0].second));
}

TEST_F(StatsThreadLocalStoreTest, ExtractAndAppendTagsRegexValueWithMatch) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  envoy::config::metrics::v3::StatsConfig stats_config;
  auto* tag_specifier = stats_config.add_stats_tags();
  tag_specifier->set_tag_name("foo_tag");
  tag_specifier->set_regex("^foo.(.+)");

  const Stats::TagVector tags_vector;
  store_->setTagProducer(TagProducerImpl::createTagProducer(stats_config, tags_vector).value());

  StatNamePool pool(symbol_table_);
  StatNameTagVector tags{{pool.add("a"), pool.add("b")}};
  store_->extractAndAppendTags(pool.add("foo.bar"), pool, tags);

  ASSERT_EQ(2, tags.size());
  EXPECT_EQ("a", symbol_table_.toString(tags[0].first));
  EXPECT_EQ("b", symbol_table_.toString(tags[0].second));
  EXPECT_EQ("foo_tag", symbol_table_.toString(tags[1].first));
  EXPECT_EQ("bar", symbol_table_.toString(tags[1].second));
}

TEST_F(StatsThreadLocalStoreTest, ExtractAndAppendTagsRegexBuiltinExpression) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  envoy::config::metrics::v3::StatsConfig stats_config;
  const Stats::TagVector tags_vector;
  store_->setTagProducer(TagProducerImpl::createTagProducer(stats_config, tags_vector).value());

  StatNamePool pool(symbol_table_);
  StatNameTagVector tags{{pool.add("a"), pool.add("b")}};
  store_->extractAndAppendTags(pool.add("cluster.foo.bar"), pool, tags);

  ASSERT_EQ(2, tags.size());
  EXPECT_EQ("a", symbol_table_.toString(tags[0].first));
  EXPECT_EQ("b", symbol_table_.toString(tags[0].second));
  EXPECT_EQ("envoy.cluster_name", symbol_table_.toString(tags[1].first));
  EXPECT_EQ("foo", symbol_table_.toString(tags[1].second));
}

class LookupWithStatNameTest : public ThreadLocalStoreNoMocksMixin, public testing::Test {};

TEST_F(LookupWithStatNameTest, All) {
  ScopeSharedPtr scope1 = scope_.scopeFromStatName(makeStatName("scope1"));
  Counter& c1 = scope_.counterFromStatName(makeStatName("c1"));
  Counter& c2 = scope1->counterFromStatName(makeStatName("c2"));
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = scope_.gaugeFromStatName(makeStatName("g1"), Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromStatName(makeStatName("g2"), Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g1.tags().size());

  Histogram& h1 = scope_.histogramFromStatName(makeStatName("h1"), Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromStatName(makeStatName("h2"), Histogram::Unit::Unspecified);
  store_->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopeSharedPtr scope2 = scope1->scopeFromStatName(makeStatName("foo"));
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromStatName(makeStatName("bar")).name());

  // Validate that we sanitize away bad characters in the stats prefix. This happens only
  // when constructing a stat from a string, not from a stat name.
  ScopeSharedPtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());
}

TEST_F(LookupWithStatNameTest, NotFound) {
  StatName not_found(makeStatName("not_found"));
  EXPECT_FALSE(scope_.findCounter(not_found));
  EXPECT_FALSE(scope_.findGauge(not_found));
  EXPECT_FALSE(scope_.findHistogram(not_found));
  EXPECT_FALSE(scope_.findTextReadout(not_found));
}

class StatsMatcherTLSTest : public StatsThreadLocalStoreTest {
public:
  envoy::config::metrics::v3::StatsConfig stats_config_;

  ~StatsMatcherTLSTest() override {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
  }

  // Adds counters for 1000 clusters and returns the amount of memory consumed.
  uint64_t memoryConsumedAddingClusterStats() {
    StatNamePool pool(symbol_table_);
    std::vector<StatName> stat_names;
    TestUtil::forEachSampleStat(1000, false, [&pool, &stat_names](absl::string_view name) {
      stat_names.push_back(pool.add(name));
    });

    {
      Memory::TestUtil::MemoryTest memory_test;
      for (StatName stat_name : stat_names) {
        scope_.counterFromStatName(stat_name);
      }
      return memory_test.consumedBytes();
    }
  }
};

TEST_F(StatsMatcherTLSTest, TestNoOpStatImpls) {
  InSequence s;

  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "noop");
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  // Testing No-op counters, gauges, histograms which match the prefix "noop".

  // Counter
  Counter& noop_counter = scope_.counterFromString("noop_counter");
  EXPECT_EQ(noop_counter.name(), "");
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.add(1);
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.inc();
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.reset();
  EXPECT_EQ(noop_counter.value(), 0);
  Counter& noop_counter_2 = scope_.counterFromString("noop_counter_2");
  EXPECT_EQ(&noop_counter, &noop_counter_2);
  EXPECT_FALSE(noop_counter.used());      // hardcoded to return false in NullMetricImpl.
  EXPECT_EQ(0, noop_counter.latch());     // hardcoded to 0.
  EXPECT_EQ(0, noop_counter.use_count()); // null counter is contained in ThreadLocalStoreImpl.

  // Gauge
  Gauge& noop_gauge = scope_.gaugeFromString("noop_gauge", Gauge::ImportMode::Accumulate);
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

  Gauge& noop_gauge_2 = scope_.gaugeFromString("noop_gauge_2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(&noop_gauge, &noop_gauge_2);

  // TextReadout
  TextReadout& noop_string = scope_.textReadoutFromString("noop_string");
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
  TextReadout& noop_string_2 = scope_.textReadoutFromString("noop_string_2");
  EXPECT_EQ(&noop_string, &noop_string_2);

  // Histogram
  Histogram& noop_histogram =
      scope_.histogramFromString("noop_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(noop_histogram.name(), "");
  EXPECT_FALSE(noop_histogram.used());
  EXPECT_EQ(Histogram::Unit::Null, noop_histogram.unit());
  Histogram& noop_histogram_2 =
      scope_.histogramFromString("noop_histogram_2", Histogram::Unit::Unspecified);
  EXPECT_EQ(&noop_histogram, &noop_histogram_2);
}

// We only test the exclusion list -- the inclusion list is the inverse, and both are tested in
// test/common/stats:stats_matcher_test.
TEST_F(StatsMatcherTLSTest, TestExclusionRegex) {
  InSequence s;

  // Expected to alloc lowercase_counter, lowercase_gauge, valid_counter, valid_gauge

  // Will block all stats containing any capital alphanumeric letter.
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->MergeFrom(
      TestUtility::createRegexMatcher(".*[A-Z].*"));
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  // The creation of counters/gauges/histograms which have no uppercase letters should succeed.
  Counter& lowercase_counter = scope_.counterFromString("lowercase_counter");
  EXPECT_EQ(lowercase_counter.name(), "lowercase_counter");
  Gauge& lowercase_gauge = scope_.gaugeFromString("lowercase_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(lowercase_gauge.name(), "lowercase_gauge");
  Histogram& lowercase_histogram =
      scope_.histogramFromString("lowercase_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(lowercase_histogram.name(), "lowercase_histogram");

  TextReadout& lowercase_string = scope_.textReadoutFromString("lowercase_string");
  EXPECT_EQ(lowercase_string.name(), "lowercase_string");
  // And the creation of counters/gauges/histograms which have uppercase letters should fail.
  Counter& uppercase_counter = scope_.counterFromString("UPPERCASE_counter");
  EXPECT_EQ(uppercase_counter.name(), "");
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);

  Gauge& uppercase_gauge = scope_.gaugeFromString("uppercase_GAUGE", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(uppercase_gauge.name(), "");
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);

  TextReadout& uppercase_string = scope_.textReadoutFromString("uppercase_STRING");
  EXPECT_EQ(uppercase_string.name(), "");
  uppercase_string.set("A STRING VALUE");
  EXPECT_EQ("", uppercase_string.value());

  // Histograms are harder to query and test, so we resort to testing that name() returns the empty
  // string.
  Histogram& uppercase_histogram =
      scope_.histogramFromString("upperCASE_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(uppercase_histogram.name(), "");

  // Adding another exclusion rule -- now we reject not just uppercase stats but those starting with
  // the string "invalid".
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "invalid");
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Counter& valid_counter = scope_.counterFromString("valid_counter");
  valid_counter.inc();
  EXPECT_EQ(valid_counter.value(), 1);

  Counter& invalid_counter = scope_.counterFromString("invalid_counter");
  invalid_counter.inc();
  EXPECT_EQ(invalid_counter.value(), 0);

  // But the old exclusion rule still holds.
  Counter& invalid_counter_2 = scope_.counterFromString("also_INVALID_counter");
  invalid_counter_2.inc();
  EXPECT_EQ(invalid_counter_2.value(), 0);

  // And we expect the same behavior from gauges and histograms.
  Gauge& valid_gauge = scope_.gaugeFromString("valid_gauge", Gauge::ImportMode::Accumulate);
  valid_gauge.set(2);
  EXPECT_EQ(valid_gauge.value(), 2);

  Gauge& invalid_gauge_1 = scope_.gaugeFromString("invalid_gauge", Gauge::ImportMode::Accumulate);
  invalid_gauge_1.inc();
  EXPECT_EQ(invalid_gauge_1.value(), 0);

  Gauge& invalid_gauge_2 =
      scope_.gaugeFromString("also_INVALID_gauge", Gauge::ImportMode::Accumulate);
  invalid_gauge_2.inc();
  EXPECT_EQ(invalid_gauge_2.value(), 0);

  Histogram& valid_histogram =
      scope_.histogramFromString("valid_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(valid_histogram.name(), "valid_histogram");

  Histogram& invalid_histogram_1 =
      scope_.histogramFromString("invalid_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(invalid_histogram_1.name(), "");

  Histogram& invalid_histogram_2 =
      scope_.histogramFromString("also_INVALID_histogram", Histogram::Unit::Unspecified);
  EXPECT_EQ(invalid_histogram_2.name(), "");

  TextReadout& valid_string = scope_.textReadoutFromString("valid_string");
  valid_string.set("i'm valid");
  EXPECT_EQ("i'm valid", valid_string.value());

  TextReadout& invalid_string_1 = scope_.textReadoutFromString("invalid_string");
  invalid_string_1.set("nope");
  EXPECT_EQ("", invalid_string_1.value());

  TextReadout& invalid_string_2 = scope_.textReadoutFromString("also_INVLD_string");
  invalid_string_2.set("still no");
  EXPECT_EQ("", invalid_string_2.value());
}

// Rejecting stats of the form "cluster." enables an optimization in the matcher
// infrastructure that performs the rejection without converting from StatName
// to string, obviating the need to memoize the rejection in a set. This saves
// a ton of memory, allowing us to reject thousands of stats without consuming
// memory. Note that the trailing "." is critical so we can compare symbolically.
TEST_F(StatsMatcherTLSTest, RejectPrefixDot) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "cluster."); // Prefix match can be executed symbolically.
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  uint64_t mem_consumed = memoryConsumedAddingClusterStats();

  // No memory is consumed at all while rejecting stats from "prefix."
  EXPECT_MEMORY_EQ(mem_consumed, 0);
  EXPECT_MEMORY_LE(mem_consumed, 0);
}

// Repeating the same test but retaining the dot means that the StatsMatcher
// infrastructure requires us remember the rejected StatNames in an ever-growing
// map. That map is needed to avoid taking locks while re-rejecting stats we've
// rejected in the past.
TEST_F(StatsMatcherTLSTest, RejectPrefixNoDot) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "cluster"); // No dot at the end means we have to compare as strings.
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  uint64_t mem_consumed = memoryConsumedAddingClusterStats();

  // Memory is consumed at all while rejecting stats from "prefix" in proportion
  // to the number of stat instantiations attempted.
  EXPECT_MEMORY_EQ(mem_consumed, 2936480);
  EXPECT_MEMORY_LE(mem_consumed, 3500000);
}

TEST_F(StatsMatcherTLSTest, DoNotRejectHiddenPrefixExclusion) {
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "cluster.");
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Gauge& accumulate_gauge =
      scope_.gaugeFromString("cluster.accumulate_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(accumulate_gauge.name(), "");
  Gauge& hidden_gauge =
      scope_.gaugeFromString("cluster.hidden_gauge", Gauge::ImportMode::HiddenAccumulate);
  EXPECT_EQ(hidden_gauge.name(), "cluster.hidden_gauge");
}

TEST_F(StatsMatcherTLSTest, DoNotRejectHiddenPrefixInclusive) {
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_prefix(
      "cluster.");
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Gauge& accumulate_gauge =
      scope_.gaugeFromString("accumulate_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(accumulate_gauge.name(), "");
  Gauge& hidden_gauge = scope_.gaugeFromString("hidden_gauge", Gauge::ImportMode::HiddenAccumulate);
  EXPECT_EQ(hidden_gauge.name(), "hidden_gauge");
}

TEST_F(StatsMatcherTLSTest, DoNotRejectHiddenExclusionRegex) {
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->MergeFrom(
      TestUtility::createRegexMatcher(".*"));
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Gauge& accumulate_gauge =
      scope_.gaugeFromString("accumulate_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(accumulate_gauge.name(), "");
  Gauge& hidden_gauge = scope_.gaugeFromString("hidden_gauge", Gauge::ImportMode::HiddenAccumulate);
  EXPECT_EQ(hidden_gauge.name(), "hidden_gauge");
}

TEST_F(StatsMatcherTLSTest, DoNotRejectHiddenInclusionRegex) {
  envoy::config::metrics::v3::StatsConfig stats_config_;
  // Create inclusion list to only accept names that have at least one capital letter.
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->MergeFrom(
      TestUtility::createRegexMatcher(".*[A-Z].*"));
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Gauge& accumulate_gauge =
      scope_.gaugeFromString("accumulate_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(accumulate_gauge.name(), "");
  Gauge& hidden_gauge = scope_.gaugeFromString("hidden_gauge", Gauge::ImportMode::HiddenAccumulate);
  EXPECT_EQ(hidden_gauge.name(), "hidden_gauge");
}

TEST_F(StatsMatcherTLSTest, DoNotRejectAllHidden) {
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->set_reject_all(true);
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));

  Gauge& accumulate_gauge =
      scope_.gaugeFromString("accumulate_gauge", Gauge::ImportMode::Accumulate);
  EXPECT_EQ(accumulate_gauge.name(), "");
  Gauge& hidden_gauge = scope_.gaugeFromString("hidden_gauge", Gauge::ImportMode::HiddenAccumulate);
  EXPECT_EQ(hidden_gauge.name(), "hidden_gauge");
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
    tls_.shutdownGlobalThreading();
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

    StatNamePool pool(symbol_table_);
    StatsMatcher::FastResult no_fast_rejection = StatsMatcher::FastResult::NoMatch;
    for (int j = 0; j < 5; ++j) {
      EXPECT_CALL(*matcher, fastRejects(pool.add("scope.reject")))
          .WillOnce(Return(no_fast_rejection));
      if (j == 0) {
        EXPECT_CALL(*matcher, slowRejects(no_fast_rejection, pool.add("scope.reject")))
            .WillOnce(Return(true));
      }
      EXPECT_EQ("", lookup_stat("reject"));
      EXPECT_CALL(*matcher, fastRejects(pool.add("scope.ok"))).WillOnce(Return(no_fast_rejection));
      if (j == 0) {
        EXPECT_CALL(*matcher, slowRejects(no_fast_rejection, pool.add("scope.ok")))
            .WillOnce(Return(false));
      }
      EXPECT_EQ("scope.ok", lookup_stat("ok"));
    }
  }

  void testRejectsAll(const LookupStatFn lookup_stat) {
    InSequence s;

    MockStatsMatcher* matcher = new MockStatsMatcher;
    matcher->rejects_all_ = true;
    StatsMatcherPtr matcher_ptr(matcher);
    store_.setStatsMatcher(std::move(matcher_ptr));

    ScopeSharedPtr scope = store_.createScope("scope.");

    StatNamePool pool(symbol_table_);
    for (int j = 0; j < 5; ++j) {
      // Note: zero calls to fastReject() or slowReject() are made, as
      // reject-all should short-circuit.
      EXPECT_EQ("", lookup_stat("reject"));
    }
  }

  void testAcceptsAll(const LookupStatFn lookup_stat) {
    InSequence s;

    auto* matcher = new MockStatsMatcher;
    matcher->accepts_all_ = true;
    StatsMatcherPtr matcher_ptr(matcher);
    store_.setStatsMatcher(std::move(matcher_ptr));
    StatNamePool pool(symbol_table_);

    StatsMatcher::FastResult no_fast_rejection = StatsMatcher::FastResult::NoMatch;
    for (int j = 0; j < 5; ++j) {
      EXPECT_CALL(*matcher, fastRejects(pool.add("scope.ok"))).WillOnce(Return(no_fast_rejection));
      // Note: zero calls to slowReject() are made, as accept-all should short-circuit.
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
      return scope_->histogramFromString(stat_name, Histogram::Unit::Unspecified).name();
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
  ScopeSharedPtr scope_;
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
  Counter& counter = scope_.counterFromString("c1");
  Gauge& gauge = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Histogram& histogram = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  TextReadout& textReadout = scope_.textReadoutFromString("t1");
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
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config, symbol_table_, context_));

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
  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

// Verify that asking for deleted stats by name does not create new copies on
// the allocator.
TEST_F(StatsThreadLocalStoreTest, AskForRejectedStat) {
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  Counter& counter = scope_.counterFromString("c1");
  Gauge& gauge = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  TextReadout& text_readout = scope_.textReadoutFromString("t1");
  ASSERT_EQ(1, store_->counters().size()); // "c1".
  ASSERT_EQ(1, store_->gauges().size());
  ASSERT_EQ(1, store_->textReadouts().size());

  // Will effectively block all stats, and remove all the non-matching stats.
  envoy::config::metrics::v3::StatsConfig stats_config;
  stats_config.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_exact(
      "no-such-stat");
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config, symbol_table_, context_));

  // They can no longer be found.
  EXPECT_EQ(0, store_->counters().size());
  EXPECT_EQ(0, store_->gauges().size());
  EXPECT_EQ(0, store_->textReadouts().size());

  // Ask for the rejected stats again by name.
  Counter& counter2 = scope_.counterFromString("c1");
  Gauge& gauge2 = scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  TextReadout& text_readout2 = scope_.textReadoutFromString("t1");

  // Verify we got the same stats.
  EXPECT_EQ(&counter, &counter2);
  EXPECT_EQ(&gauge, &gauge2);
  EXPECT_EQ(&text_readout, &text_readout2);

  // Verify that new stats were not created.
  EXPECT_EQ(0, store_->counters().size());
  EXPECT_EQ(0, store_->gauges().size());
  EXPECT_EQ(0, store_->textReadouts().size());

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, NonHotRestartNoTruncation) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Allocate a stat greater than the max name length.
  const std::string name_1(MaxStatNameLength + 1, 'A');

  scope_.counterFromString(name_1);

  // This works fine, and we can find it by its long name because heap-stats do not
  // get truncated.
  EXPECT_NE(nullptr, TestUtility::findCounter(*store_, name_1).get());
  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

class StatsThreadLocalStoreTestNoFixture : public testing::Test {
protected:
  StatsThreadLocalStoreTestNoFixture()
      : alloc_(symbol_table_), store_(alloc_), scope_(*store_.rootScope()) {
    store_.addSink(sink_);

    // Use a tag producer that will produce tags.
    envoy::config::metrics::v3::StatsConfig stats_config;
    const Stats::TagVector tags_vector;
    store_.setTagProducer(TagProducerImpl::createTagProducer(stats_config, tags_vector).value());
  }

  ~StatsThreadLocalStoreTestNoFixture() override {
    if (threading_enabled_) {
      tls_.shutdownGlobalThreading();
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
  Scope& scope_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  bool threading_enabled_{false};
};

// Tests how much memory is consumed allocating 100k stats.
TEST_F(StatsThreadLocalStoreTestNoFixture, MemoryWithoutTlsRealSymbolTable) {
  Memory::TestUtil::MemoryTest memory_test;
  TestUtil::forEachSampleStat(
      100, true, [this](absl::string_view name) { scope_.counterFromString(std::string(name)); });
  EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 688080); // July 2, 2020
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 0.85 * million_);
}

TEST_F(StatsThreadLocalStoreTestNoFixture, MemoryWithTlsRealSymbolTable) {
  initThreading();
  Memory::TestUtil::MemoryTest memory_test;
  TestUtil::forEachSampleStat(
      100, true, [this](absl::string_view name) { scope_.counterFromString(std::string(name)); });
  EXPECT_MEMORY_EQ(memory_test.consumedBytes(), 827616); // Sep 25, 2020
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 0.99 * million_);
}

TEST_F(StatsThreadLocalStoreTest, ShuttingDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  scope_.counterFromString("c1");
  scope_.gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  scope_.textReadoutFromString("t1");
  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  scope_.counterFromString("c2");
  scope_.gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  scope_.textReadoutFromString("t2");

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

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();
  tls_.shutdownThread();
}

TEST_F(StatsThreadLocalStoreTest, MergeDuringShutDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  tls_.shutdownGlobalThreading();
  store_->shutdownThreading();

  // Validate that merge callback is called during shutdown and there is no ASSERT.
  bool merge_called = false;
  store_->mergeHistograms([&merge_called]() -> void { merge_called = true; });

  EXPECT_TRUE(merge_called);
  tls_.shutdownGlobalThreading();
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
  { ScopeSharedPtr scope1 = store.createScope("scope1."); }
  tls.shutdownGlobalThreading();
  store.shutdownThreading();
  tls.shutdownThread();
}

// Histogram tests
TEST_F(HistogramTest, BasicSingleHistogramMerge) {
  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
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
  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope_.histogramFromString("h2", Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  expectCallAndAccumulate(h1, 1);
  expectCallAndAccumulate(h2, 1);
  expectCallAndAccumulate(h2, 2);

  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, MultiHistogramMultipleMerges) {
  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope_.histogramFromString("h2", Histogram::Unit::Unspecified);
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
  ScopeSharedPtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Histogram::Unit::Unspecified);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());

  expectCallAndAccumulate(h1, 2);
  expectCallAndAccumulate(h2, 2);
  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, BasicHistogramSummaryValidate) {
  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope_.histogramFromString("h2", Histogram::Unit::Unspecified);

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
  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);

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
  ScopeSharedPtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = scope_.histogramFromString("h1", Histogram::Unit::Unspecified);
  Histogram& h2 = scope1->histogramFromString("h2", Histogram::Unit::Unspecified);
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

TEST_F(HistogramTest, ParentHistogramBucketSummaryAndDetail) {
  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  Histogram& histogram = scope_.histogramFromString("histogram", Histogram::Unit::Unspecified);
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
  EXPECT_THAT(parent_histogram->detailedTotalBuckets(), UnorderedElementsAre(Bucket{10, 1, 1}));
  EXPECT_THAT(parent_histogram->detailedIntervalBuckets(), UnorderedElementsAre(Bucket{10, 1, 1}));
}

TEST_F(HistogramTest, ForEachHistogram) {
  std::vector<std::reference_wrapper<Histogram>> histograms;

  const size_t num_stats = 11;
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = absl::StrCat("histogram.", idx);
    histograms.emplace_back(scope_.histogramFromString(stat_name, Histogram::Unit::Unspecified));
  }
  EXPECT_EQ(histograms.size(), 11);

  size_t num_histograms = 0;
  size_t num_iterations = 0;
  store_->forEachHistogram([&num_histograms](std::size_t size) { num_histograms = size; },
                           [&num_iterations](ParentHistogram&) { ++num_iterations; });
  EXPECT_EQ(num_histograms, 11);
  EXPECT_EQ(num_iterations, 11);

  Histogram& deleted_histogram = histograms[4];

  // Verify that rejecting histograms removes them from the iteration set.
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->set_reject_all(true);
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  num_histograms = 0;
  num_iterations = 0;
  store_->forEachHistogram([&num_histograms](std::size_t size) { num_histograms = size; },
                           [&num_iterations](ParentHistogram&) { ++num_iterations; });
  EXPECT_EQ(num_histograms, 0);
  EXPECT_EQ(num_iterations, 0);

  // Verify that we can access the local reference without a crash.
  EXPECT_EQ(deleted_histogram.unit(), Histogram::Unit::Unspecified);
}

class OneWorkerThread : public ThreadLocalRealThreadsMixin, public testing::Test {
protected:
  static constexpr uint32_t NumThreads = 1;
  OneWorkerThread() : ThreadLocalRealThreadsMixin(NumThreads) {}
};

// Reproduces a race-condition between forEachScope and scope deletion. If we
// replace the code in ThreadLocalStoreImpl::forEachScope with this:
//
// SPELLCHECKER(off)
//     Thread::LockGuard lock(lock_);
//     if (f_size != nullptr) {
//       f_size(scopes_.size());
//     }
//     for (auto iter : scopes_) {
//       if (iter.first != default_scope_.get()) {
//         sync_.syncPoint(ThreadLocalStoreImpl::IterateScopeSync);
//       }
//       f_scope(*(iter.first));
//     }
// SPELLCHECKER(on)
//
// then we'll get a fatal exception on a weak_ptr conversion with this test.
TEST_F(OneWorkerThread, DeleteForEachRace) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  std::vector<ConstScopeSharedPtr> scopes;

  store_->sync().enable();
  store_->sync().waitOn(ThreadLocalStoreImpl::DeleteScopeSync);
  store_->sync().waitOn(ThreadLocalStoreImpl::IterateScopeSync);
  auto wait_for_worker = runOnAllWorkers([this, &scopes]() {
    store_->forEachScope(
        nullptr, [&scopes](const Scope& scope) { scopes.push_back(scope.getConstShared()); });
    EXPECT_EQ(1, scopes.size());
  });
  store_->sync().barrierOn(ThreadLocalStoreImpl::IterateScopeSync);
  auto wait_for_main = runOnMain([&scope]() { scope.reset(); });
  store_->sync().barrierOn(ThreadLocalStoreImpl::DeleteScopeSync);
  store_->sync().signal(ThreadLocalStoreImpl::IterateScopeSync);
  wait_for_worker();
  store_->sync().signal(ThreadLocalStoreImpl::DeleteScopeSync);
  wait_for_main();
}

class ClusterShutdownCleanupStarvationTest : public ThreadLocalRealThreadsMixin,
                                             public testing::Test {
protected:
  static constexpr uint32_t NumThreads = 2;

  ClusterShutdownCleanupStarvationTest()
      : ThreadLocalRealThreadsMixin(NumThreads), my_counter_name_(pool_.add("my_counter")),
        my_counter_scoped_name_(pool_.add("scope.my_counter")),
        start_time_(time_system_.monotonicTime()) {}

  void createScopesIncCountersAndCleanup() {
    for (uint32_t i = 0; i < NumScopes; ++i) {
      ScopeSharedPtr scope = store_->createScope("scope.");
      Counter& counter = scope->counterFromStatName(my_counter_name_);
      counter.inc();
    }
  }

  void createScopesIncCountersAndCleanupAllThreads() {
    runOnAllWorkersBlocking([this]() { createScopesIncCountersAndCleanup(); });
  }

  std::chrono::seconds elapsedTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(time_system_.monotonicTime() -
                                                            start_time_);
  }

  Event::TestRealTimeSystem time_system_;
  StatName my_counter_name_;
  StatName my_counter_scoped_name_;
  MonotonicTime start_time_;
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

class HistogramThreadTest : public ThreadLocalRealThreadsMixin, public testing::Test {
protected:
  static constexpr uint32_t NumThreads = 10;

  HistogramThreadTest() : ThreadLocalRealThreadsMixin(NumThreads) {}

  void mergeHistograms() {
    BlockingBarrier blocking_barrier(1);
    runOnMainBlocking([this, &blocking_barrier]() {
      store_->mergeHistograms(blocking_barrier.decrementCountFn());
    });
  }

  uint32_t numTlsHistograms() {
    uint32_t num;
    {
      BlockingBarrier blocking_barrier(1);
      runOnMainBlocking([this, &num, &blocking_barrier]() {
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
    runOnAllWorkersBlocking([&fn]() { fn(); });
  }
};

TEST_F(HistogramThreadTest, MakeHistogramsAndRecordValues) {
  foreachThread([this]() {
    Histogram& histogram = scope_.histogramFromString("my_hist", Histogram::Unit::Unspecified);
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
  ScopeSharedPtr scope1 = store_->createScope("scope.");
  ScopeSharedPtr scope2 = store_->createScope("scope.");
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
  // There should be no more ParentHistograms or TlsHistograms.
  scope2.reset();
  histograms.clear();
  mergeHistograms();

  EXPECT_EQ(0, store_->histograms().size());
  EXPECT_EQ(0, numTlsHistograms());

  shutdownThreading();
  scope_.histogramFromString("histogram_after_shutdown", Histogram::Unit::Unspecified);
}

class TestSinkPredicates : public Stats::SinkPredicates {
public:
  bool includeCounter(const Stats::Counter&) override { return (++num_counters_) % 10 == 0; }
  bool includeGauge(const Stats::Gauge&) override { return (++num_gauges_) % 10 == 0; }
  bool includeTextReadout(const Stats::TextReadout&) override {
    return (++num_text_readouts_) % 10 == 0;
  }
  bool includeHistogram(const Stats::Histogram&) override { return false; }

private:
  size_t num_counters_ = 0;
  size_t num_gauges_ = 0;
  size_t num_text_readouts_ = 0;
};

TEST_F(StatsThreadLocalStoreTest, SetSinkPredicates) {
  constexpr int num_stats = 20;
  static constexpr int expected_sinked_stats = 2; // 10% of 20.
  StatNamePool pool(store_->symbolTable());

  store_->setSinkPredicates(std::make_unique<TestSinkPredicates>());

  // Create counters
  for (uint64_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = pool.add(absl::StrCat("counter.", idx));
    scope_.counterFromStatName(stat_name).inc();
  }
  // Create gauges
  for (uint64_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = pool.add(absl::StrCat("gauge.", idx));
    scope_.gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::NeverImport).set(idx);
  }

  // Create text readouts
  for (uint64_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = pool.add(absl::StrCat("text_readout.", idx));
    scope_.textReadoutFromStatName(stat_name).set(store_->symbolTable().toString(stat_name));
  }

  uint32_t num_sinked_counters = 0, num_sinked_gauges = 0, num_sinked_text_readouts = 0;
  auto check_expected_size = [](size_t size) { EXPECT_EQ(expected_sinked_stats, size); };

  store_->forEachSinkedCounter(check_expected_size,
                               [&num_sinked_counters](Counter&) { ++num_sinked_counters; });
  EXPECT_EQ(expected_sinked_stats, num_sinked_counters);

  store_->forEachSinkedGauge(check_expected_size,
                             [&num_sinked_gauges](Gauge&) { ++num_sinked_gauges; });
  EXPECT_EQ(expected_sinked_stats, num_sinked_gauges);

  store_->forEachSinkedTextReadout(check_expected_size, [&num_sinked_text_readouts](TextReadout&) {
    ++num_sinked_text_readouts;
  });
  EXPECT_EQ(expected_sinked_stats, num_sinked_text_readouts);
}

enum class EnableIncludeHistograms { No = 0, Yes };
class HistogramParameterisedTest : public HistogramTest,
                                   public ::testing::WithParamInterface<EnableIncludeHistograms> {
public:
  HistogramParameterisedTest() { local_info_.node_.set_cluster(""); }

protected:
  void SetUp() override {
    HistogramTest::SetUp();

    // Set the feature flag in SetUp as store_ is constructed in HistogramTest::SetUp.
    api_ = Api::createApiForTest(*store_);
    ProtobufWkt::Struct base = TestUtility::parseYaml<ProtobufWkt::Struct>(
        GetParam() == EnableIncludeHistograms::Yes ? R"EOF(
    envoy.reloadable_features.enable_include_histograms: true
    )EOF"
                                                   : R"EOF(
    envoy.reloadable_features.enable_include_histograms: false
    )EOF");
    envoy::config::bootstrap::v3::LayeredRuntime layered_runtime;
    {
      auto* layer = layered_runtime.add_layers();
      layer->set_name("base");
      layer->mutable_static_layer()->MergeFrom(base);
    }
    {
      auto* layer = layered_runtime.add_layers();
      layer->set_name("admin");
      layer->mutable_admin_layer();
    }
    absl::StatusOr<std::unique_ptr<Runtime::LoaderImpl>> loader =
        Runtime::LoaderImpl::create(dispatcher_, tls_, layered_runtime, local_info_, *store_,
                                    generator_, validation_visitor_, *api_);
    THROW_IF_NOT_OK(loader.status());
    loader_ = std::move(loader.value());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Event::MockDispatcher dispatcher_;
  Api::ApiPtr api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Random::MockRandomGenerator generator_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  std::unique_ptr<Runtime::LoaderImpl> loader_;
};

TEST_P(HistogramParameterisedTest, ForEachSinkedHistogram) {
  std::unique_ptr<TestUtil::TestSinkPredicates> test_sink_predicates =
      std::make_unique<TestUtil::TestSinkPredicates>();
  std::vector<std::reference_wrapper<Histogram>> sinked_histograms;
  std::vector<std::reference_wrapper<Histogram>> unsinked_histograms;
  auto scope = store_->rootScope();

  const size_t num_stats = 11;
  // Create some histograms before setting the predicates.
  for (size_t idx = 0; idx < num_stats / 2; ++idx) {
    auto name = absl::StrCat("histogram.", idx);
    StatName stat_name = pool_.add(name);
    //  sink every 3rd stat
    if ((idx + 1) % 3 == 0) {
      test_sink_predicates->add(stat_name);
      sinked_histograms.emplace_back(
          scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));
    } else {
      unsinked_histograms.emplace_back(
          scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));
    }
  }

  store_->setSinkPredicates(std::move(test_sink_predicates));
  auto& sink_predicates = testSinkPredicatesOrDie();

  // Create some histograms after setting the predicates.
  for (size_t idx = num_stats / 2; idx < num_stats; ++idx) {
    auto name = absl::StrCat("histogram.", idx);
    StatName stat_name = pool_.add(name);
    // sink every 3rd stat
    if ((idx + 1) % 3 == 0) {
      sink_predicates.add(stat_name);
      sinked_histograms.emplace_back(
          scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));
    } else {
      unsinked_histograms.emplace_back(
          scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));
    }
  }

  EXPECT_EQ(sinked_histograms.size(), 3);
  EXPECT_EQ(unsinked_histograms.size(), 8);

  size_t num_sinked_histograms = 0;
  size_t num_iterations = 0;
  store_->forEachSinkedHistogram(
      [&num_sinked_histograms](std::size_t size) { num_sinked_histograms = size; },
      [&num_iterations, &sink_predicates](ParentHistogram& histogram) {
        if (GetParam() == EnableIncludeHistograms::Yes) {
          EXPECT_TRUE(sink_predicates.has(histogram.statName()));
        }
        ++num_iterations;
      });
  if (GetParam() == EnableIncludeHistograms::Yes) {
    EXPECT_EQ(num_sinked_histograms, 3);
    EXPECT_EQ(num_iterations, 3);
  } else {
    EXPECT_EQ(num_sinked_histograms, 11);
    EXPECT_EQ(num_iterations, 11);
  }
  // Verify that rejecting histograms removes them from the sink set.
  envoy::config::metrics::v3::StatsConfig stats_config_;
  stats_config_.mutable_stats_matcher()->set_reject_all(true);
  store_->setStatsMatcher(
      std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_));
  num_sinked_histograms = 0;
  num_iterations = 0;
  store_->forEachSinkedHistogram(
      [&num_sinked_histograms](std::size_t size) { num_sinked_histograms = size; },
      [&num_iterations](ParentHistogram&) { ++num_iterations; });
  EXPECT_EQ(num_sinked_histograms, 0);
  EXPECT_EQ(num_iterations, 0);
}

// Verify that histograms that are not flushed to sinks are merged in the call
// to mergeHistograms
TEST_P(HistogramParameterisedTest, UnsinkedHistogramsAreMerged) {
  store_->setSinkPredicates(std::make_unique<TestUtil::TestSinkPredicates>());
  auto& sink_predicates = testSinkPredicatesOrDie();
  StatName stat_name = pool_.add("h1");
  sink_predicates.add(stat_name);
  auto scope = store_->rootScope();

  auto& h1 = static_cast<ParentHistogramImpl&>(
      scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));
  stat_name = pool_.add("h2");
  auto& h2 = static_cast<ParentHistogramImpl&>(
      scope->histogramFromStatName(stat_name, Histogram::Unit::Unspecified));

  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 5));
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 5));

  h1.recordValue(5);
  h2.recordValue(5);

  EXPECT_THAT(h1.cumulativeStatistics().bucketSummary(), HasSubstr(" B10: 0,"));
  EXPECT_THAT(h2.cumulativeStatistics().bucketSummary(), HasSubstr(" B10: 0,"));

  // Verify that all the histograms have not been merged yet.
  EXPECT_EQ(h1.used(), false);
  EXPECT_EQ(h2.used(), false);

  store_->mergeHistograms([this, &sink_predicates]() -> void {
    size_t num_iterations = 0;
    size_t num_sinked_histograms = 0;
    store_->forEachSinkedHistogram(
        [&num_sinked_histograms](std::size_t size) { num_sinked_histograms = size; },
        [&num_iterations, &sink_predicates](ParentHistogram& histogram) {
          if (GetParam() == EnableIncludeHistograms::Yes) {
            EXPECT_TRUE(sink_predicates.has(histogram.statName()));
          }
          ++num_iterations;
        });
    if (GetParam() == EnableIncludeHistograms::Yes) {
      EXPECT_EQ(num_sinked_histograms, 1);
      EXPECT_EQ(num_iterations, 1);
    } else {
      EXPECT_EQ(num_sinked_histograms, 2);
      EXPECT_EQ(num_iterations, 2);
    }
  });

  EXPECT_THAT(h1.cumulativeStatistics().bucketSummary(), HasSubstr(" B10: 1,"));
  EXPECT_THAT(h2.cumulativeStatistics().bucketSummary(), HasSubstr(" B10: 1,"));
  EXPECT_EQ(h1.cumulativeStatistics().bucketSummary(), h2.cumulativeStatistics().bucketSummary());

  // Verify that all the histograms have been merged.
  EXPECT_EQ(h1.used(), true);
  EXPECT_EQ(h2.used(), true);
}

INSTANTIATE_TEST_SUITE_P(HistogramParameterisedTestGroup, HistogramParameterisedTest,
                         testing::Values(EnableIncludeHistograms::Yes, EnableIncludeHistograms::No),
                         [](const testing::TestParamInfo<EnableIncludeHistograms>& info) {
                           return info.param == EnableIncludeHistograms::No
                                      ? "DisableIncludeHistograms"
                                      : "EnableIncludeHistograms";
                         });
} // namespace Stats
} // namespace Envoy
