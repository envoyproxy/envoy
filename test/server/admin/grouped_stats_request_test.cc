#include <memory>

#include "envoy/stats/histogram.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/server/admin/stats_request_test_base.h"

using testing::ReturnRef;

namespace Envoy {
namespace Server {

class GroupedStatsRequestTest : public StatsRequestTestBase<GroupedStatsRequest> {
protected:
  std::unique_ptr<GroupedStatsRequest> makeRequest(bool used_only, bool text_readouts = false) {
    StatsParams params;
    params.used_only_ = used_only;
    params.prometheus_text_readouts_ = text_readouts;
    params.format_ = StatsFormat::Prometheus;
    return std::make_unique<GroupedStatsRequest>(store_, params, custom_namespaces_);
  }

  void addCounter(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    store_.rootScope()->counterFromStatNameWithTags(makeStatName(name), cluster_tags);
  }

  void addGauge(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    store_.rootScope()->gaugeFromStatNameWithTags(makeStatName(name), cluster_tags,
                                                  Stats::Gauge::ImportMode::Accumulate);
  }

  void addTextReadout(const std::string& name, const std::string& value,
                      Stats::StatNameTagVector cluster_tags) {
    Stats::TextReadout& textReadout =
        store_.rootScope()->textReadoutFromStatNameWithTags(makeStatName(name), cluster_tags);
    textReadout.set(value);
  }

  Stats::CustomStatNamespacesImpl custom_namespaces_;
};

class PrometheusStatsRenderingTest : public testing::Test {
protected:
  PrometheusStatsRenderingTest() : alloc_(*symbol_table_), pool_(*symbol_table_) {}

  ~PrometheusStatsRenderingTest() override { clearStorage(); }

  Stats::StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  std::unique_ptr<GroupedStatsRequest> makeRequest(bool used_only, bool text_readouts = false) {
    StatsParams params;
    params.used_only_ = used_only;
    params.prometheus_text_readouts_ = text_readouts;
    params.format_ = StatsFormat::Prometheus;
    return std::make_unique<GroupedStatsRequest>(mock_store_, params, custom_namespaces_);
  }

  std::unique_ptr<GroupedStatsRequest> makeRequest(StatsParams& params) {
    return std::make_unique<GroupedStatsRequest>(mock_store_, params, custom_namespaces_);
  }

  void addCounter(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    test_scope_ptr_->counters_.push_back(alloc_.makeCounter(
        name_storage.statName(), tag_extracted_name_storage.statName(), cluster_tags));
  }

  void addGauge(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    test_scope_ptr_->gauges_.push_back(
        alloc_.makeGauge(name_storage.statName(), tag_extracted_name_storage.statName(),
                         cluster_tags, Stats::Gauge::ImportMode::Accumulate));
  }

  void addTextReadout(const std::string& name, const std::string& value,
                      Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    Stats::TextReadoutSharedPtr textReadout = alloc_.makeTextReadout(
        name_storage.statName(), tag_extracted_name_storage.statName(), cluster_tags);
    textReadout->set(value);
    test_scope_ptr_->text_readouts_.push_back(textReadout);
  }

  // Format tags into the name to create a unique stat_name for each name:tag combination.
  // If the same stat_name is passed to makeGauge() or makeCounter(), even with different
  // tags, a copy of the previous metric will be returned.
  std::string baseName(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    std::string result = name;
    for (const auto& name_tag : cluster_tags) {
      result.append(fmt::format("<{}:{}>", symbol_table_->toString(name_tag.first),
                                symbol_table_->toString(name_tag.second)));
    }
    return result;
  }

  using MockHistogramSharedPtr = Stats::RefcountPtr<testing::NiceMock<Stats::MockParentHistogram>>;
  void addHistogram(MockHistogramSharedPtr histogram) {
    test_scope_ptr_->histograms_.push_back(histogram);
  }

  MockHistogramSharedPtr makeHistogram(const std::string& name,
                                       Stats::StatNameTagVector cluster_tags) {
    auto histogram = MockHistogramSharedPtr(new testing::NiceMock<Stats::MockParentHistogram>());
    histogram->name_ = baseName(name, cluster_tags);
    histogram->setTagExtractedName(name);
    histogram->setTags(cluster_tags);
    histogram->used_ = true;
    return histogram;
  }

  std::string response(GroupedStatsRequest& request) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    EXPECT_EQ(Http::Code::OK, code);
    Buffer::OwnedImpl data;
    while (request.nextChunk(data)) {
    }
    return data.toString();
  }

  void clearStorage() {
    pool_.clear();
    test_scope_ptr_->counters_.clear();
    test_scope_ptr_->histograms_.clear();
    test_scope_ptr_->gauges_.clear();
    test_scope_ptr_->text_readouts_.clear();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::StatNamePool pool_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
  testing::NiceMock<MockStore> mock_store_;
  std::shared_ptr<TestScope> test_scope_ptr_ = std::make_shared<TestScope>("", mock_store_);
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

  void setHistogramValuesWithCounts(const std::vector<std::pair<uint64_t, uint64_t>>& values) {
    for (std::pair<uint64_t, uint64_t> cv : values) {
      hist_insert_intscale(histogram_, cv.first, 0, cv.second);
    }
  }

private:
  histogram_t* histogram_;
};

TEST_F(GroupedStatsRequestTest, Empty) { EXPECT_EQ(0, iterateChunks(*makeRequest(false))); }

TEST_F(GroupedStatsRequestTest, OneCounter) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  Stats::Counter& c1 = store_.rootScope()->counterFromStatNameWithTags(
      makeStatName("cluster.upstream.cx.total"), c1Tags);
  c1.add(10);

  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(GroupedStatsRequestTest, OneGauge) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.rootScope()->gaugeFromStatNameWithTags(makeStatName("foo"), c1Tags,
                                                Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(GroupedStatsRequestTest, OneHistogram) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.rootScope()->histogramFromStatNameWithTags(makeStatName("foo"), c1Tags,
                                                    Stats::Histogram::Unit::Milliseconds);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(GroupedStatsRequestTest, OneTextReadout) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.rootScope()->textReadoutFromStatNameWithTags(makeStatName("foo"), c1Tags);
  // text readouts are not included in the returned prometheus stats, unless specifically asked for
  // via query param
  EXPECT_EQ(0, iterateChunks(*makeRequest(false)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, true)));
}

TEST_F(GroupedStatsRequestTest, OneScope) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(false)));
}

// example output:
// # TYPE envoy_foo6 counter
// envoy_foo6{cluster="c1"} 0
TEST_F(GroupedStatsRequestTest, ManyStatsSmallChunkSize) {
  for (uint32_t i = 0; i < 10; ++i) {
    Stats::StatNameTagVector tags{{makeStatName("cluster"), makeStatName("c1")}};
    store_.rootScope()->counterFromStatNameWithTags(makeStatName(absl::StrCat("foo", i)), tags);
  }
  std::unique_ptr<GroupedStatsRequest> request = makeRequest(false);
  request->setChunkSize(50);
  EXPECT_EQ(10, iterateChunks(*request));
}

TEST_F(GroupedStatsRequestTest, ManyStatsSmallChunkSizeNoDrain) {
  for (uint32_t i = 0; i < 10; ++i) {
    Stats::StatNameTagVector tags{{makeStatName("cluster"), makeStatName("c1")}};
    store_.rootScope()->counterFromStatNameWithTags(makeStatName(absl::StrCat("foo", i)), tags);
  }
  std::unique_ptr<GroupedStatsRequest> request = makeRequest(false);
  request->setChunkSize(50);
  EXPECT_EQ(10, iterateChunks(*request, false));
}

TEST_F(GroupedStatsRequestTest, OneStatUsedOnly) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(true)));
}

TEST_F(GroupedStatsRequestTest, MetricNameCollison) {
  // Create two counters and two gauges with each pair having the same name,
  // but having different tag names and values. 2 groups should be rendered.

  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStatName("a.tag-name"), makeStatName("a.tag-value")}});
  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStatName("another_tag_name"), makeStatName("another_tag-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStatName("another_tag_name_3"), makeStatName("another_tag_3-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStatName("another_tag_name_4"), makeStatName("another_tag_4-value")}});

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_cluster_1_upstream_cx_total counter
envoy_cluster_test_cluster_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
envoy_cluster_test_cluster_1_upstream_cx_total{another_tag_name="another_tag-value"} 0
# TYPE envoy_cluster_test_cluster_2_upstream_cx_total gauge
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(GroupedStatsRequestTest, UniqueMetricName) {
  // Create two counters and two gauges, all with unique names.
  // 4 groups should be rendered.

  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStatName("a.tag-name"), makeStatName("a.tag-value")}});
  addCounter("cluster.test_cluster_2.upstream_cx_total",
             {{makeStatName("another_tag_name"), makeStatName("another_tag-value")}});
  addGauge("cluster.test_cluster_3.upstream_cx_total",
           {{makeStatName("another_tag_name_3"), makeStatName("another_tag_3-value")}});
  addGauge("cluster.test_cluster_4.upstream_cx_total",
           {{makeStatName("another_tag_name_4"), makeStatName("another_tag_4-value")}});

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_cluster_1_upstream_cx_total counter
envoy_cluster_test_cluster_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
# TYPE envoy_cluster_test_cluster_2_upstream_cx_total counter
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name="another_tag-value"} 0
# TYPE envoy_cluster_test_cluster_3_upstream_cx_total gauge
envoy_cluster_test_cluster_3_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
# TYPE envoy_cluster_test_cluster_4_upstream_cx_total gauge
envoy_cluster_test_cluster_4_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, HistogramWithNoValuesAndNoTags) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);
  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 0
envoy_histogram1_bucket{le="1"} 0
envoy_histogram1_bucket{le="5"} 0
envoy_histogram1_bucket{le="10"} 0
envoy_histogram1_bucket{le="25"} 0
envoy_histogram1_bucket{le="50"} 0
envoy_histogram1_bucket{le="100"} 0
envoy_histogram1_bucket{le="250"} 0
envoy_histogram1_bucket{le="500"} 0
envoy_histogram1_bucket{le="1000"} 0
envoy_histogram1_bucket{le="2500"} 0
envoy_histogram1_bucket{le="5000"} 0
envoy_histogram1_bucket{le="10000"} 0
envoy_histogram1_bucket{le="30000"} 0
envoy_histogram1_bucket{le="60000"} 0
envoy_histogram1_bucket{le="300000"} 0
envoy_histogram1_bucket{le="600000"} 0
envoy_histogram1_bucket{le="1800000"} 0
envoy_histogram1_bucket{le="3600000"} 0
envoy_histogram1_bucket{le="+Inf"} 0
envoy_histogram1_sum{} 0
envoy_histogram1_count{} 0
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, HistogramWithNonDefaultBuckets) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::ConstSupportedBuckets buckets{10, 20};
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(
      h1_cumulative.getHistogram(), Stats::Histogram::Unit::Unspecified, buckets);

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="10"} 0
envoy_histogram1_bucket{le="20"} 0
envoy_histogram1_bucket{le="+Inf"} 0
envoy_histogram1_sum{} 0
envoy_histogram1_count{} 0
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

// Test that scaled percents are emitted in the expected 0.0-1.0 range, and that the buckets
// apply to the final output range, not the internal scaled range.
TEST_F(PrometheusStatsRenderingTest, HistogramWithScaledPercent) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::ConstSupportedBuckets buckets{0.5, 1.0};

  constexpr double scale_factor = Stats::Histogram::PercentScale;
  h1_cumulative.setHistogramValuesWithCounts(std::vector<std::pair<uint64_t, uint64_t>>({
      {0.25 * scale_factor, 1},
      {0.75 * scale_factor, 1},
      {1.25 * scale_factor, 1},
  }));

  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram(),
                                                          Stats::Histogram::Unit::Percent, buckets);

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 1
envoy_histogram1_bucket{le="1"} 2
envoy_histogram1_bucket{le="+Inf"} 3
envoy_histogram1_sum{} 2.2599999999999997868371792719699
envoy_histogram1_count{} 3
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, HistogramWithHighCounts) {
  HistogramWrapper h1_cumulative;

  // Force large counts to prove that the +Inf bucket doesn't overflow to scientific notation.
  h1_cumulative.setHistogramValuesWithCounts(std::vector<std::pair<uint64_t, uint64_t>>({
      {1, 100000},
      {100, 1000000},
      {1000, 100000000},
  }));

  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 0
envoy_histogram1_bucket{le="1"} 0
envoy_histogram1_bucket{le="5"} 100000
envoy_histogram1_bucket{le="10"} 100000
envoy_histogram1_bucket{le="25"} 100000
envoy_histogram1_bucket{le="50"} 100000
envoy_histogram1_bucket{le="100"} 100000
envoy_histogram1_bucket{le="250"} 1100000
envoy_histogram1_bucket{le="500"} 1100000
envoy_histogram1_bucket{le="1000"} 1100000
envoy_histogram1_bucket{le="2500"} 101100000
envoy_histogram1_bucket{le="5000"} 101100000
envoy_histogram1_bucket{le="10000"} 101100000
envoy_histogram1_bucket{le="30000"} 101100000
envoy_histogram1_bucket{le="60000"} 101100000
envoy_histogram1_bucket{le="300000"} 101100000
envoy_histogram1_bucket{le="600000"} 101100000
envoy_histogram1_bucket{le="1800000"} 101100000
envoy_histogram1_bucket{le="3600000"} 101100000
envoy_histogram1_bucket{le="+Inf"} 101100000
envoy_histogram1_sum{} 105105105000
envoy_histogram1_count{} 101100000
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, OutputWithAllMetricTypes) {
  custom_namespaces_.registerStatNamespace("promtest");

  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStatName("a.tag-name"), makeStatName("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStatName("another_tag_name"), makeStatName("another_tag-value")}});
  addCounter("promtest.myapp.test.foo", {{makeStatName("tag_name"), makeStatName("tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStatName("another_tag_name_3"), makeStatName("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStatName("another_tag_name_4"), makeStatName("another_tag_4-value")}});
  addGauge("promtest.MYAPP.test.bar", {{makeStatName("tag_name"), makeStatName("tag-value")}});
  // Metric with invalid prometheus namespace in the custom metric must be excluded in the output.
  addGauge("promtest.1234abcd.test.bar", {{makeStatName("tag_name"), makeStatName("tag-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram("cluster.test_1.upstream_rq_time",
                                  {{makeStatName("key1"), makeStatName("value1")},
                                   {makeStatName("key2"), makeStatName("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
# TYPE envoy_cluster_test_2_upstream_cx_total counter
envoy_cluster_test_2_upstream_cx_total{another_tag_name="another_tag-value"} 0
# TYPE myapp_test_foo counter
myapp_test_foo{tag_name="tag-value"} 0
# TYPE envoy_cluster_test_3_upstream_cx_total gauge
envoy_cluster_test_3_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
# TYPE envoy_cluster_test_4_upstream_cx_total gauge
envoy_cluster_test_4_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
# TYPE MYAPP_test_bar gauge
MYAPP_test_bar{tag_name="tag-value"} 0
# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 1
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 2
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 4
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5532
envoy_cluster_test_1_upstream_rq_time_count{key1="value1",key2="value2"} 7
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, OutputWithTextReadoutsInGaugeFormat) {
  addCounter("cluster.upstream_cx_total_count", {{makeStatName("cluster"), makeStatName("c1")}});
  addGauge("cluster.upstream_cx_total", {{makeStatName("cluster"), makeStatName("c1")}});
  // Text readouts that should be returned in gauge format.
  addTextReadout("control_plane.identifier", "CP-1",
                 {{makeStatName("cluster"), makeStatName("c1")}});
  addTextReadout("invalid_tag_values", "test",
                 {{makeStatName("tag1"), makeStatName(R"(\)")},
                  {makeStatName("tag2"), makeStatName("\n")},
                  {makeStatName("tag3"), makeStatName(R"(")")}});

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_upstream_cx_total_count counter
envoy_cluster_upstream_cx_total_count{cluster="c1"} 0
# TYPE envoy_cluster_upstream_cx_total gauge
envoy_cluster_upstream_cx_total{cluster="c1"} 0
# TYPE envoy_control_plane_identifier gauge
envoy_control_plane_identifier{cluster="c1",text_value="CP-1"} 0
# TYPE envoy_invalid_tag_values gauge
envoy_invalid_tag_values{tag1="\\",tag2="\n",tag3="\"",text_value="test"} 0
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false, true)));
}

// Test that output groups all metrics of the same name (with different tags) together,
// as required by the Prometheus exposition format spec. Additionally, groups of metrics
// should be sorted by their tags; the format specifies that it is preferred that metrics
// are always grouped in the same order, and sorting is an easy way to ensure this.
TEST_F(PrometheusStatsRenderingTest, OutputSortedByMetricName) {
  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  // Create the 3 clusters in non-sorted order to exercise the sorting.
  // Create two of each metric type (counter, gauge, histogram) so that
  // the output for each needs to be collected together.
  for (const char* cluster : {"ccc", "aaa", "bbb"}) {
    const Stats::StatNameTagVector tags{{makeStatName("cluster"), makeStatName(cluster)}};
    addCounter("cluster.upstream_cx_total", tags);
    addCounter("cluster.upstream_cx_connect_fail", tags);
    addGauge("cluster.upstream_cx_active", tags);
    addGauge("cluster.upstream_rq_active", tags);

    for (const char* hist_name : {"cluster.upstream_rq_time", "cluster.upstream_response_time"}) {
      auto histogram1 = makeHistogram(hist_name, tags);
      histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
      addHistogram(histogram1);
      EXPECT_CALL(*histogram1, cumulativeStatistics())
          .WillOnce(ReturnRef(h1_cumulative_statistics));
    }
  }

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_upstream_cx_connect_fail counter
envoy_cluster_upstream_cx_connect_fail{cluster="aaa"} 0
envoy_cluster_upstream_cx_connect_fail{cluster="bbb"} 0
envoy_cluster_upstream_cx_connect_fail{cluster="ccc"} 0
# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="aaa"} 0
envoy_cluster_upstream_cx_total{cluster="bbb"} 0
envoy_cluster_upstream_cx_total{cluster="ccc"} 0
# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="aaa"} 0
envoy_cluster_upstream_cx_active{cluster="bbb"} 0
envoy_cluster_upstream_cx_active{cluster="ccc"} 0
# TYPE envoy_cluster_upstream_rq_active gauge
envoy_cluster_upstream_rq_active{cluster="aaa"} 0
envoy_cluster_upstream_rq_active{cluster="bbb"} 0
envoy_cluster_upstream_rq_active{cluster="ccc"} 0
# TYPE envoy_cluster_upstream_response_time histogram
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="0.5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="1"} 0
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="10"} 0
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="25"} 1
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="50"} 2
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="100"} 4
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="5000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="aaa"} 5532
envoy_cluster_upstream_response_time_count{cluster="aaa"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="0.5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="10"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="25"} 1
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="50"} 2
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="100"} 4
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="5000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="bbb"} 5532
envoy_cluster_upstream_response_time_count{cluster="bbb"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="0.5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="10"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="25"} 1
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="50"} 2
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="100"} 4
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="5000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="ccc"} 5532
envoy_cluster_upstream_response_time_count{cluster="ccc"} 7
# TYPE envoy_cluster_upstream_rq_time histogram
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="50"} 2
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="100"} 4
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="5000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="aaa"} 5532
envoy_cluster_upstream_rq_time_count{cluster="aaa"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="50"} 2
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="100"} 4
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="5000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="bbb"} 5532
envoy_cluster_upstream_rq_time_count{cluster="bbb"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="50"} 2
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="100"} 4
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="5000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="ccc"} 5532
envoy_cluster_upstream_rq_time_count{cluster="ccc"} 7
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(false)));
}

TEST_F(PrometheusStatsRenderingTest, OutputWithUsedOnly) {
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStatName("a.tag-name"), makeStatName("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStatName("another_tag_name"), makeStatName("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStatName("another_tag_name_3"), makeStatName("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStatName("another_tag_name_4"), makeStatName("another_tag_4-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram("cluster.test_1.upstream_rq_time",
                                  {{makeStatName("key1"), makeStatName("value1")},
                                   {makeStatName("key2"), makeStatName("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 1
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 2
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 4
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5532
envoy_cluster_test_1_upstream_rq_time_count{key1="value1",key2="value2"} 7
)EOF";

  EXPECT_EQ(expected_output, response(*makeRequest(true)));
}

TEST_F(PrometheusStatsRenderingTest, OutputWithUsedOnlyHistogram) {
  const std::vector<uint64_t> h1_values = {};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram("cluster.test_1.upstream_rq_time",
                                  {{makeStatName("key1"), makeStatName("value1")},
                                   {makeStatName("key2"), makeStatName("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->used_ = false;
  addHistogram(histogram1);

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  {
    EXPECT_CALL(*histogram1, cumulativeStatistics()).Times(0);

    std::string result = response(*makeRequest(true));
    EXPECT_EQ(EMPTY_STRING, result);
  }

  {
    EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

    const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 0
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 0
envoy_cluster_test_1_upstream_rq_time_count{key1="value1",key2="value2"} 0
)EOF";

    EXPECT_EQ(expected_output, response(*makeRequest(false)));
  }
}

TEST_F(PrometheusStatsRenderingTest, OutputWithRegexp) {
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStatName("a.tag-name"), makeStatName("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStatName("another_tag_name"), makeStatName("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStatName("another_tag_name_3"), makeStatName("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStatName("another_tag_name_4"), makeStatName("another_tag_4-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram("cluster.test_1.upstream_rq_time",
                                  {{makeStatName("key1"), makeStatName("value1")},
                                   {makeStatName("key2"), makeStatName("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);

  ON_CALL(mock_store_, rootScope()).WillByDefault(testing::Return(test_scope_ptr_));

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
)EOF";
  {
    Buffer::OwnedImpl res;
    StatsParams params;
    ASSERT_EQ(Http::Code::OK, params.parse("/stats?filter=cluster.test_1.upstream_cx_total", res));
    params.format_ = StatsFormat::Prometheus;
    EXPECT_EQ(expected_output, response(*makeRequest(params)));
  }

  {
    Buffer::OwnedImpl res;
    StatsParams params;
    ASSERT_EQ(Http::Code::OK,
              params.parse("/stats?filter=cluster.test_1.upstream_cx_total&safe", res));
    params.format_ = StatsFormat::Prometheus;
    EXPECT_EQ(expected_output, response(*makeRequest(params)));
  }
}

} // namespace Server
} // namespace Envoy
