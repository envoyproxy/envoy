#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/config/metrics/v2/stats.pb.h"

#include "common/common/c_smart_ptr.h"
#include "common/memory/stats.h"
#include "common/stats/stats_matcher_impl.h"
#include "common/stats/tag_producer_impl.h"
#include "common/stats/thread_local_store.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Stats {

class StatsThreadLocalStoreTest : public testing::Test {
public:
  void SetUp() override {
    alloc_ = std::make_unique<MockedTestAllocator>(options_);
    resetStoreWithAlloc(*alloc_);
  }

  void resetStoreWithAlloc(StatDataAllocator& alloc) {
    store_ = std::make_unique<ThreadLocalStoreImpl>(options_, alloc);
    store_->addSink(sink_);
  }

  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  StatsOptionsImpl options_;
  std::unique_ptr<MockedTestAllocator> alloc_;
  MockSink sink_;
  std::unique_ptr<ThreadLocalStoreImpl> store_;
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
  typedef std::map<std::string, ParentHistogramSharedPtr> NameHistogramMap;

  HistogramTest() : alloc_(options_) {}

  void SetUp() override {
    store_ = std::make_unique<ThreadLocalStoreImpl>(options_, alloc_);
    store_->addSink(sink_);
    store_->initializeThreading(main_thread_dispatcher_, tls_);
  }

  void TearDown() override {
    store_->shutdownThreading();
    tls_.shutdownThread();
    // Includes overflow stat.
    EXPECT_CALL(alloc_, free(_));
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
    EXPECT_EQ(h1->cumulativeStatistics().summary(), h1_cumulative_statistics.summary());
    EXPECT_EQ(h1->intervalStatistics().summary(), h1_interval_statistics.summary());

    if (histogram_list.size() > 1) {
      const ParentHistogramSharedPtr& h2 = name_histogram_map["h2"];
      EXPECT_EQ(h2->cumulativeStatistics().summary(), h2_cumulative_statistics.summary());
      EXPECT_EQ(h2->intervalStatistics().summary(), h2_interval_statistics.summary());
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

  MOCK_METHOD1(alloc, RawStatData*(const std::string& name));
  MOCK_METHOD1(free, void(RawStatData& data));

  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  StatsOptionsImpl options_;
  MockedTestAllocator alloc_;
  MockSink sink_;
  std::unique_ptr<ThreadLocalStoreImpl> store_;
  InSequence s;
  std::vector<uint64_t> h1_cumulative_values_, h2_cumulative_values_, h1_interval_values_,
      h2_interval_values_;
};

TEST_F(StatsThreadLocalStoreTest, NoTls) {
  InSequence s;
  EXPECT_CALL(*alloc_, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Histogram& h1 = store_->histogram("h1");
  EXPECT_EQ(&h1, &store_->histogram("h1"));

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  store_->deliverHistogramToSinks(h1, 100);

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_)).Times(3);

  store_->shutdownThreading();
}

TEST_F(StatsThreadLocalStoreTest, Tls) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*alloc_, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Histogram& h1 = store_->histogram("h1");
  EXPECT_EQ(&h1, &store_->histogram("h1"));

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(3L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(3L, store_->gauges().front().use_count());

  store_->shutdownThreading();
  tls_.shutdownThread();

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, TestUtility::findCounter(*store_, "c1").get());
  EXPECT_EQ(2L, TestUtility::findCounter(*store_, "c1").use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get()); // front() ok when size()==1
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, BasicScope) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*alloc_, alloc(_)).Times(4);
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
  EXPECT_CALL(*alloc_, free(_)).Times(5);
}

// Validate that we sanitize away bad characters in the stats prefix.
TEST_F(StatsThreadLocalStoreTest, SanitizePrefix) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope(std::string("scope1:\0:foo.", 13));
  EXPECT_CALL(*alloc_, alloc(_));
  Counter& c1 = scope1->counter("c1");
  EXPECT_EQ("scope1___foo.c1", c1.name());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_)).Times(2);
}

TEST_F(StatsThreadLocalStoreTest, ScopeDelete) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*alloc_, alloc(_));
  scope1->counter("c1");
  EXPECT_EQ(2UL, store_->counters().size());
  CounterSharedPtr c1 = TestUtility::findCounter(*store_, "scope1.c1");
  EXPECT_EQ("scope1.c1", c1->name());
  EXPECT_EQ(TestUtility::findByName(store_->source().cachedCounters(), "scope1.c1"), c1);

  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(tls_, runOnAllThreads(_));
  scope1.reset();
  EXPECT_EQ(1UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->source().cachedCounters().size());
  store_->source().clearCache();
  EXPECT_EQ(1UL, store_->source().cachedCounters().size());

  EXPECT_CALL(*alloc_, free(_));
  EXPECT_EQ(1L, c1.use_count());
  c1.reset();

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_));
}

TEST_F(StatsThreadLocalStoreTest, NestedScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*alloc_, alloc(_));
  Counter& c1 = scope1->counter("foo.bar");
  EXPECT_EQ("scope1.foo.bar", c1.name());

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_CALL(*alloc_, alloc(_));
  Counter& c2 = scope2->counter("bar");
  EXPECT_NE(&c1, &c2);
  EXPECT_EQ("scope1.foo.bar", c2.name());

  // Different allocations point to the same referenced counted backing memory.
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(c1.value(), c2.value());

  EXPECT_CALL(*alloc_, alloc(_));
  Gauge& g1 = scope2->gauge("some_gauge");
  EXPECT_EQ("scope1.foo.some_gauge", g1.name());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_)).Times(4);
}

TEST_F(StatsThreadLocalStoreTest, OverlappingScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Both scopes point to the same namespace. This can happen during reload of a cluster for
  // example.
  ScopePtr scope1 = store_->createScope("scope1.");
  ScopePtr scope2 = store_->createScope("scope1.");

  // We will call alloc twice, but they should point to the same backing storage.
  EXPECT_CALL(*alloc_, alloc(_)).Times(2);
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
  EXPECT_CALL(*alloc_, alloc(_)).Times(2);
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
  EXPECT_CALL(*alloc_, free(_)).Times(2);
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
  EXPECT_CALL(*alloc_, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, AllocFailed) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*alloc_, alloc(absl::string_view("foo"))).WillOnce(Return(nullptr));
  Counter& c1 = store_->counter("foo");
  EXPECT_EQ(1UL, store_->counter("stats.overflow").value());

  c1.inc();
  EXPECT_EQ(1UL, c1.value());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow but not the failsafe stat which we allocated from the heap.
  EXPECT_CALL(*alloc_, free(_));
}

TEST_F(StatsThreadLocalStoreTest, HotRestartTruncation) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // First, with a successful RawStatData allocation:
  const uint64_t max_name_length = options_.maxNameLength();
  const std::string name_1(max_name_length + 1, 'A');

  EXPECT_CALL(*alloc_, alloc(_));
  EXPECT_LOG_CONTAINS("warning", "is too long with", store_->counter(name_1));

  // The stats did not overflow yet.
  EXPECT_EQ(0UL, store_->counter("stats.overflow").value());

  // The name will be truncated, so we won't be able to find it with the entire name.
  EXPECT_EQ(nullptr, TestUtility::findCounter(*store_, name_1).get());

  // But we can find it based on the expected truncation.
  EXPECT_NE(nullptr, TestUtility::findCounter(*store_, name_1.substr(0, max_name_length)).get());

  // The same should be true with heap allocation, which occurs when the default
  // allocator fails.
  const std::string name_2(max_name_length + 1, 'B');
  EXPECT_CALL(*alloc_, alloc(_)).WillOnce(Return(nullptr));
  store_->counter(name_2);

  // Same deal: the name will be truncated, so we won't be able to find it with the entire name.
  EXPECT_EQ(nullptr, TestUtility::findCounter(*store_, name_1).get());

  // But we can find it based on the expected truncation.
  EXPECT_NE(nullptr, TestUtility::findCounter(*store_, name_1.substr(0, max_name_length)).get());

  // Now the stats have overflowed.
  EXPECT_EQ(1UL, store_->counter("stats.overflow").value());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow, and the first raw-allocated stat, but not the failsafe stat which we
  // allocated from the heap.
  EXPECT_CALL(*alloc_, free(_)).Times(2);
}

class StatsMatcherTLSTest : public StatsThreadLocalStoreTest {
public:
  StatsMatcherTLSTest() : StatsThreadLocalStoreTest() {}
  envoy::config::metrics::v2::StatsConfig stats_config_;
};

TEST_F(StatsMatcherTLSTest, TestNoOpStatImpls) {
  InSequence s;

  EXPECT_CALL(*alloc_, alloc(_)).Times(0);

  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "noop");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  // Testing No-op counters, gauges, histograms which match the prefix "noop".

  // Counter
  Counter& noop_counter = store_->counter("noop_counter");
  EXPECT_EQ(noop_counter.name(), "");
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.add(1);
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.inc();
  EXPECT_EQ(noop_counter.value(), 0);
  noop_counter.reset();
  EXPECT_EQ(noop_counter.value(), 0);
  Counter& noop_counter_2 = store_->counter("noop_counter_2");
  EXPECT_EQ(&noop_counter, &noop_counter_2);

  // Gauge
  Gauge& noop_gauge = store_->gauge("noop_gauge");
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
  Gauge& noop_gauge_2 = store_->gauge("noop_gauge_2");
  EXPECT_EQ(&noop_gauge, &noop_gauge_2);

  // Histogram
  Histogram& noop_histogram = store_->histogram("noop_histogram");
  EXPECT_EQ(noop_histogram.name(), "");
  Histogram& noop_histogram_2 = store_->histogram("noop_histogram_2");
  EXPECT_EQ(&noop_histogram, &noop_histogram_2);

  // Includes overflow stat.
  EXPECT_CALL(*alloc_, free(_)).Times(1);

  store_->shutdownThreading();
}

// We only test the exclusion list -- the inclusion list is the inverse, and both are tested in
// test/common/stats:stats_matcher_test.
TEST_F(StatsMatcherTLSTest, TestExclusionRegex) {
  InSequence s;

  // Expected to alloc lowercase_counter, lowercase_gauge, valid_counter, valid_gauge
  EXPECT_CALL(*alloc_, alloc(_)).Times(4);

  // Will block all stats containing any capital alphanumeric letter.
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(
      ".*[A-Z].*");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  // The creation of counters/gauges/histograms which have no uppercase letters should succeed.
  Counter& lowercase_counter = store_->counter("lowercase_counter");
  EXPECT_EQ(lowercase_counter.name(), "lowercase_counter");
  Gauge& lowercase_gauge = store_->gauge("lowercase_gauge");
  EXPECT_EQ(lowercase_gauge.name(), "lowercase_gauge");
  Histogram& lowercase_histogram = store_->histogram("lowercase_histogram");
  EXPECT_EQ(lowercase_histogram.name(), "lowercase_histogram");

  // And the creation of counters/gauges/histograms which have uppercase letters should fail.
  Counter& uppercase_counter = store_->counter("UPPERCASE_counter");
  EXPECT_EQ(uppercase_counter.name(), "");
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);
  uppercase_counter.inc();
  EXPECT_EQ(uppercase_counter.value(), 0);

  Gauge& uppercase_gauge = store_->gauge("uppercase_GAUGE");
  EXPECT_EQ(uppercase_gauge.name(), "");
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);
  uppercase_gauge.inc();
  EXPECT_EQ(uppercase_gauge.value(), 0);

  // Histograms are harder to query and test, so we resort to testing that name() returns the empty
  // string.
  Histogram& uppercase_histogram = store_->histogram("upperCASE_histogram");
  EXPECT_EQ(uppercase_histogram.name(), "");

  // Adding another exclusion rule -- now we reject not just uppercase stats but those starting with
  // the string "invalid".
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "invalid");
  store_->setStatsMatcher(std::make_unique<StatsMatcherImpl>(stats_config_));

  Counter& valid_counter = store_->counter("valid_counter");
  valid_counter.inc();
  EXPECT_EQ(valid_counter.value(), 1);

  Counter& invalid_counter = store_->counter("invalid_counter");
  invalid_counter.inc();
  EXPECT_EQ(invalid_counter.value(), 0);

  // But the old exclusion rule still holds.
  Counter& invalid_counter_2 = store_->counter("also_INVALID_counter");
  invalid_counter_2.inc();
  EXPECT_EQ(invalid_counter_2.value(), 0);

  // And we expect the same behavior from gauges and histograms.
  Gauge& valid_gauge = store_->gauge("valid_gauge");
  valid_gauge.set(2);
  EXPECT_EQ(valid_gauge.value(), 2);

  Gauge& invalid_gauge_1 = store_->gauge("invalid_gauge");
  invalid_gauge_1.inc();
  EXPECT_EQ(invalid_gauge_1.value(), 0);

  Gauge& invalid_gauge_2 = store_->gauge("also_INVALID_gauge");
  invalid_gauge_2.inc();
  EXPECT_EQ(invalid_gauge_2.value(), 0);

  Histogram& valid_histogram = store_->histogram("valid_histogram");
  EXPECT_EQ(valid_histogram.name(), "valid_histogram");

  Histogram& invalid_histogram_1 = store_->histogram("invalid_histogram");
  EXPECT_EQ(invalid_histogram_1.name(), "");

  Histogram& invalid_histogram_2 = store_->histogram("also_INVALID_histogram");
  EXPECT_EQ(invalid_histogram_2.name(), "");

  // Expected to free lowercase_counter, lowercase_gauge, valid_counter,
  // valid_gauge, overflow.stats
  EXPECT_CALL(*alloc_, free(_)).Times(5);

  store_->shutdownThreading();
}

class HeapStatsThreadLocalStoreTest : public StatsThreadLocalStoreTest {
public:
  void SetUp() override {
    resetStoreWithAlloc(heap_alloc_);
    // Note: we do not call StatsThreadLocalStoreTest::SetUp here as that
    // sets up a thread_local_store with raw stat alloc.
  }
  void TearDown() override {
    store_->shutdownThreading();
    tls_.shutdownThread();
    store_.reset(); // delete before the allocator.
  }

  HeapStatDataAllocator heap_alloc_;
};

TEST_F(HeapStatsThreadLocalStoreTest, NonHotRestartNoTruncation) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Allocate a stat greater than the max name length.
  const uint64_t max_name_length = options_.maxNameLength();
  const std::string name_1(max_name_length + 1, 'A');

  store_->counter(name_1);

  // This works fine, and we can find it by its long name because heap-stats do not
  // get truncsated.
  EXPECT_NE(nullptr, TestUtility::findCounter(*store_, name_1).get());
}

// Tests how much memory is consumed allocating 100k stats.
TEST_F(HeapStatsThreadLocalStoreTest, MemoryWithoutTls) {
  if (!TestUtil::hasDeterministicMallocStats()) {
    return;
  }

  // Use a tag producer that will produce tags.
  envoy::config::metrics::v2::StatsConfig stats_config;
  store_->setTagProducer(std::make_unique<TagProducerImpl>(stats_config));

  const size_t million = 1000 * 1000;
  const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();
  if (start_mem == 0) {
    // Skip this test for platforms where we can't measure memory.
    return;
  }
  TestUtil::forEachSampleStat(
      1000, [this](absl::string_view name) { store_->counter(std::string(name)); });
  const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
  EXPECT_LT(start_mem, end_mem);
  EXPECT_LT(end_mem - start_mem, 28 * million); // actual value: 27203216 as of Oct 29, 2018
}

TEST_F(HeapStatsThreadLocalStoreTest, MemoryWithTls) {
  if (!TestUtil::hasDeterministicMallocStats()) {
    return;
  }

  // Use a tag producer that will produce tags.
  envoy::config::metrics::v2::StatsConfig stats_config;
  store_->setTagProducer(std::make_unique<TagProducerImpl>(stats_config));

  const size_t million = 1000 * 1000;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();
  if (start_mem == 0) {
    // Skip this test for platforms where we can't measure memory.
    return;
  }
  TestUtil::forEachSampleStat(
      1000, [this](absl::string_view name) { store_->counter(std::string(name)); });
  const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
  EXPECT_LT(start_mem, end_mem);
  EXPECT_LT(end_mem - start_mem, 31 * million); // actual value: 30482576 as of Oct 29, 2018
}

TEST_F(StatsThreadLocalStoreTest, ShuttingDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*alloc_, alloc(_)).Times(4);
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
  EXPECT_CALL(*alloc_, free(_)).Times(5);
}

TEST_F(StatsThreadLocalStoreTest, MergeDuringShutDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Histogram& h1 = store_->histogram("h1");
  EXPECT_EQ("h1", h1.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  store_->shutdownThreading();

  // Validate that merge callback is called during shutdown and there is no ASSERT.
  bool merge_called = false;
  store_->mergeHistograms([&merge_called]() -> void { merge_called = true; });

  EXPECT_TRUE(merge_called);

  tls_.shutdownThread();

  EXPECT_CALL(*alloc_, free(_));
}

// Histogram tests
TEST_F(HistogramTest, BasicSingleHistogramMerge) {
  Histogram& h1 = store_->histogram("h1");
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
  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = store_->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  expectCallAndAccumulate(h1, 1);
  expectCallAndAccumulate(h2, 1);
  expectCallAndAccumulate(h2, 2);

  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, MultiHistogramMultipleMerges) {
  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = store_->histogram("h2");
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

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());

  expectCallAndAccumulate(h1, 2);
  expectCallAndAccumulate(h2, 2);
  EXPECT_EQ(2, validateMerge());
}

TEST_F(HistogramTest, BasicHistogramSummaryValidate) {
  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = store_->histogram("h2");

  expectCallAndAccumulate(h1, 1);

  EXPECT_EQ(2, validateMerge());

  const std::string h1_expected_summary =
      "P0: 1, P25: 1.025, P50: 1.05, P75: 1.075, P90: 1.09, P95: 1.095, "
      "P99: 1.099, P99.5: 1.0995, P99.9: 1.0999, P100: 1.1";
  const std::string h2_expected_summary = "P0: 0, P25: 25, P50: 50, P75: 75, P90: 90, P95: 95, "
                                          "P99: 99, P99.5: 99.5, P99.9: 99.9, P100: 100";

  for (size_t i = 0; i < 100; ++i) {
    expectCallAndAccumulate(h2, i);
  }

  EXPECT_EQ(2, validateMerge());

  NameHistogramMap name_histogram_map = makeHistogramMap(store_->histograms());
  EXPECT_EQ(h1_expected_summary, name_histogram_map["h1"]->cumulativeStatistics().summary());
  EXPECT_EQ(h2_expected_summary, name_histogram_map["h2"]->cumulativeStatistics().summary());
}

// Validates the summary after known value merge in to same histogram.
TEST_F(HistogramTest, BasicHistogramMergeSummary) {
  Histogram& h1 = store_->histogram("h1");

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

  NameHistogramMap name_histogram_map = makeHistogramMap(store_->histograms());
  EXPECT_EQ(expected_summary, name_histogram_map["h1"]->cumulativeStatistics().summary());
}

TEST_F(HistogramTest, BasicHistogramUsed) {
  ScopePtr scope1 = store_->createScope("scope1.");

  Histogram& h1 = store_->histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
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

} // namespace Stats
} // namespace Envoy
