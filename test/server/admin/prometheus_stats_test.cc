#include <regex>

#include "server/admin/prometheus_stats.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

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

class PrometheusStatsFormatterTest : public testing::Test {
protected:
  PrometheusStatsFormatterTest() : alloc_(*symbol_table_), pool_(*symbol_table_) {}

  ~PrometheusStatsFormatterTest() override { clearStorage(); }

  void addCounter(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    counters_.push_back(alloc_.makeCounter(name_storage.statName(),
                                           tag_extracted_name_storage.statName(), cluster_tags));
  }

  void addGauge(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    gauges_.push_back(alloc_.makeGauge(name_storage.statName(),
                                       tag_extracted_name_storage.statName(), cluster_tags,
                                       Stats::Gauge::ImportMode::Accumulate));
  }

  using MockHistogramSharedPtr = Stats::RefcountPtr<NiceMock<Stats::MockParentHistogram>>;
  void addHistogram(MockHistogramSharedPtr histogram) { histograms_.push_back(histogram); }

  MockHistogramSharedPtr makeHistogram(const std::string& name,
                                       Stats::StatNameTagVector cluster_tags) {
    auto histogram = MockHistogramSharedPtr(new NiceMock<Stats::MockParentHistogram>());
    histogram->name_ = baseName(name, cluster_tags);
    histogram->setTagExtractedName(name);
    histogram->setTags(cluster_tags);
    histogram->used_ = true;
    return histogram;
  }

  Stats::StatName makeStat(absl::string_view name) { return pool_.add(name); }

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

  void clearStorage() {
    pool_.clear();
    counters_.clear();
    gauges_.clear();
    histograms_.clear();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  Stats::TestSymbolTable symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::StatNamePool pool_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
};

TEST_F(PrometheusStatsFormatterTest, MetricName) {
  std::string raw = "vulture.eats-liver";
  std::string expected = "envoy_vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricName) {
  std::string raw = "An.artist.plays-violin@019street";
  std::string expected = "envoy_An_artist_plays_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricNameDigitFirst) {
  std::string raw = "3.artists.play-violin@019street";
  std::string expected = "envoy_3_artists_play_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, NamespaceRegistry) {
  std::string raw = "vulture.eats-liver";
  std::string expected = "vulture_eats_liver";

  EXPECT_FALSE(PrometheusStatsFormatter::registerPrometheusNamespace("3vulture"));
  EXPECT_FALSE(PrometheusStatsFormatter::registerPrometheusNamespace(".vulture"));

  EXPECT_FALSE(PrometheusStatsFormatter::unregisterPrometheusNamespace("vulture"));
  EXPECT_TRUE(PrometheusStatsFormatter::registerPrometheusNamespace("vulture"));
  EXPECT_FALSE(PrometheusStatsFormatter::registerPrometheusNamespace("vulture"));
  EXPECT_EQ(expected, PrometheusStatsFormatter::metricName(raw));
  EXPECT_TRUE(PrometheusStatsFormatter::unregisterPrometheusNamespace("vulture"));

  EXPECT_EQ("envoy_" + expected, PrometheusStatsFormatter::metricName(raw));
}

TEST_F(PrometheusStatsFormatterTest, FormattedTags) {
  std::vector<Stats::Tag> tags;
  Stats::Tag tag1 = {"a.tag-name", "a.tag-value"};
  Stats::Tag tag2 = {"another_tag_name", "another_tag-value"};
  tags.push_back(tag1);
  tags.push_back(tag2);
  std::string expected = "a_tag_name=\"a.tag-value\",another_tag_name=\"another_tag-value\"";
  auto actual = PrometheusStatsFormatter::formattedTags(tags);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, MetricNameCollison) {

  // Create two counters and two gauges with each pair having the same name,
  // but having different tag names and values.
  //`statsAsPrometheus()` should return two implying it found two unique stat names

  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(2UL, size);
}

TEST_F(PrometheusStatsFormatterTest, UniqueMetricName) {

  // Create two counters and two gauges, all with unique names.
  // statsAsPrometheus() should return four implying it found
  // four unique stat names.

  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_cluster_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_cluster_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_cluster_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(4UL, size);
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithNoValuesAndNoTags) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(1UL, size);

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

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithNonDefaultBuckets) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::ConstSupportedBuckets buckets{10, 20};
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram(), buckets);

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="10"} 0
envoy_histogram1_bucket{le="20"} 0
envoy_histogram1_bucket{le="+Inf"} 0
envoy_histogram1_sum{} 0
envoy_histogram1_count{} 0

)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithHighCounts) {
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

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(1UL, size);

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

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithAllMetricTypes) {
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("key1"), makeStat("value1")},
                                                        {makeStat("key2"), makeStat("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(5UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0

# TYPE envoy_cluster_test_2_upstream_cx_total counter
envoy_cluster_test_2_upstream_cx_total{another_tag_name="another_tag-value"} 0

# TYPE envoy_cluster_test_3_upstream_cx_total gauge
envoy_cluster_test_3_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0

# TYPE envoy_cluster_test_4_upstream_cx_total gauge
envoy_cluster_test_4_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0

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

  EXPECT_EQ(expected_output, response.toString());
}

// Test that output groups all metrics of the same name (with different tags) together,
// as required by the Prometheus exposition format spec. Additionally, groups of metrics
// should be sorted by their tags; the format specifies that it is preferred that metrics
// are always grouped in the same order, and sorting is an easy way to ensure this.
TEST_F(PrometheusStatsFormatterTest, OutputSortedByMetricName) {
  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  // Create the 3 clusters in non-sorted order to exercise the sorting.
  // Create two of each metric type (counter, gauge, histogram) so that
  // the output for each needs to be collected together.
  for (const char* cluster : {"ccc", "aaa", "bbb"}) {
    const Stats::StatNameTagVector tags{{makeStat("cluster"), makeStat(cluster)}};
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

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(6UL, size);

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

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithUsedOnly) {
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("key1"), makeStat("value1")},
                                                        {makeStat("key2"), makeStat("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          true, absl::nullopt);
  EXPECT_EQ(1UL, size);

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

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithUsedOnlyHistogram) {
  const std::vector<uint64_t> h1_values = {};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("key1"), makeStat("value1")},
                                                        {makeStat("key2"), makeStat("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->used_ = false;
  addHistogram(histogram1);

  {
    const bool used_only = true;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).Times(0);

    Buffer::OwnedImpl response;
    auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_,
                                                            response, used_only, absl::nullopt);
    EXPECT_EQ(0UL, size);
  }

  {
    const bool used_only = false;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

    Buffer::OwnedImpl response;
    auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_,
                                                            response, used_only, absl::nullopt);
    EXPECT_EQ(1UL, size);
  }
}

TEST_F(PrometheusStatsFormatterTest, OutputWithRegexp) {
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("key1"), makeStat("value1")},
                                                        {makeStat("key2"), makeStat("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, response, false,
      absl::optional<std::regex>{std::regex("cluster.test_1.upstream_cx_total")});
  EXPECT_EQ(1UL, size);

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0

)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

} // namespace Server
} // namespace Envoy
