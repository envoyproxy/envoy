#include <regex>
#include <string>
#include <vector>

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/prometheus_stats.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/stats_utility.h"
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
  PrometheusStatsFormatterTest()
      : alloc_(*symbol_table_), pool_(*symbol_table_),
        endpoints_helper_(std::make_unique<Upstream::PerEndpointMetricsTestHelper>()) {}

  ~PrometheusStatsFormatterTest() override { clearStorage(); }

  void addCounter(const std::string& name, Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    counters_.push_back(alloc_.makeCounter(name_storage.statName(),
                                           tag_extracted_name_storage.statName(), cluster_tags));
  }

  void addGauge(const std::string& name, Stats::StatNameTagVector cluster_tags,
                Stats::Gauge::ImportMode import_mode = Stats::Gauge::ImportMode::Accumulate) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    gauges_.push_back(alloc_.makeGauge(
        name_storage.statName(), tag_extracted_name_storage.statName(), cluster_tags, import_mode));
  }

  void addTextReadout(const std::string& name, const std::string& value,
                      Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    Stats::TextReadoutSharedPtr textReadout = alloc_.makeTextReadout(
        name_storage.statName(), tag_extracted_name_storage.statName(), cluster_tags);
    textReadout->set(value);
    textReadouts_.push_back(textReadout);
  }

  void addClusterEndpoints(const std::string& cluster_name, uint32_t num_hosts,
                           const Stats::TagVector& tags) {
    auto& cluster = endpoints_helper_->makeCluster(cluster_name, num_hosts);

    // Create a persistent copy of the tags so a reference can be captured that remains valid.
    endpoints_tags_.push_back(std::make_unique<Stats::TagVector>(tags));
    EXPECT_CALL(cluster.info_->stats_store_, fixedTags())
        .WillRepeatedly(ReturnRef(*endpoints_tags_.back()));
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
    histogram->hidden_ = false;
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
    textReadouts_.clear();
    endpoints_helper_.reset();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::StatNamePool pool_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
  std::vector<Stats::TextReadoutSharedPtr> textReadouts_;
  std::unique_ptr<Upstream::PerEndpointMetricsTestHelper> endpoints_helper_;
  std::vector<std::unique_ptr<Stats::TagVector>> endpoints_tags_;
};

TEST_F(PrometheusStatsFormatterTest, MetricName) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  std::string raw = "vulture.eats-liver";
  std::string expected = "envoy_vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw, custom_namespaces);
  EXPECT_TRUE(actual.has_value());
  EXPECT_EQ(expected, actual.value());
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricName) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  std::string raw = "An.artist.plays-violin@019street";
  std::string expected = "envoy_An_artist_plays_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw, custom_namespaces);
  EXPECT_EQ(expected, actual.value());
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricNameDigitFirst) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  std::string raw = "3.artists.play-violin@019street";
  std::string expected = "envoy_3_artists_play_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw, custom_namespaces);
  EXPECT_TRUE(actual.has_value());
  EXPECT_EQ(expected, actual.value());
}

TEST_F(PrometheusStatsFormatterTest, CustomNamespace) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  custom_namespaces.registerStatNamespace("promstattest");
  std::string raw = "promstattest.vulture.eats-liver";
  std::string expected = "vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw, custom_namespaces);
  EXPECT_TRUE(actual.has_value());
  EXPECT_EQ(expected, actual.value());
}

TEST_F(PrometheusStatsFormatterTest, CustomNamespaceWithInvalidPromnamespace) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  custom_namespaces.registerStatNamespace("promstattest");
  std::string raw = "promstattest.1234abcd.eats-liver";
  auto actual = PrometheusStatsFormatter::metricName(raw, custom_namespaces);
  EXPECT_FALSE(actual.has_value());
}

TEST_F(PrometheusStatsFormatterTest, FormattedTags) {
  std::vector<Stats::Tag> tags;
  Stats::Tag tag1 = {"a.tag-name", "a.tag-value"};
  Stats::Tag tag2 = {"another_tag_name", "another_tag-value"};
  Stats::Tag tag3 = {"replace_problematic", R"(val"ue with\ some
 issues)"};
  tags.push_back(tag1);
  tags.push_back(tag2);
  tags.push_back(tag3);
  std::string expected = "a_tag_name=\"a.tag-value\",another_tag_name=\"another_tag-value\","
                         "replace_problematic=\"val\\\"ue with\\\\ some\\n issues\"";
  auto actual = PrometheusStatsFormatter::formattedTags(tags);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, MetricNameCollison) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(2UL, size);
}

TEST_F(PrometheusStatsFormatterTest, UniqueMetricName) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(4UL, size);
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithNoValuesAndNoTags) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
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

TEST_F(PrometheusStatsFormatterTest, SummaryWithNoValuesAndNoTags) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  HistogramWrapper h1_interval;
  Stats::HistogramStatisticsImpl h1_interval_statistics(h1_interval.getHistogram());

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, intervalStatistics()).WillByDefault(ReturnRef(h1_interval_statistics));

  addHistogram(histogram);
  StatsParams params = StatsParams();
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::Summary;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
      custom_namespaces);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 summary
envoy_histogram1{quantile="0"} nan
envoy_histogram1{quantile="0.25"} nan
envoy_histogram1{quantile="0.5"} nan
envoy_histogram1{quantile="0.75"} nan
envoy_histogram1{quantile="0.9"} nan
envoy_histogram1{quantile="0.95"} nan
envoy_histogram1{quantile="0.99"} nan
envoy_histogram1{quantile="0.995"} nan
envoy_histogram1{quantile="0.999"} nan
envoy_histogram1{quantile="1"} nan
envoy_histogram1_sum{} 0
envoy_histogram1_count{} 0
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

// Replicate bug https://github.com/envoyproxy/envoy/issues/27173 which fails to
// coalesce stats in different scopes with the same tag-extracted-name.
TEST_F(PrometheusStatsFormatterTest, DifferentNamedScopeSameStat) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  Stats::ThreadLocalStoreImpl store(alloc_);
  envoy::config::metrics::v3::StatsConfig stats_config;
  const Stats::TagVector tags;
  store.setTagProducer(Stats::TagProducerImpl::createTagProducer(stats_config, tags).value());
  Stats::StatName name = pool_.add("default.total_match_count");

  Stats::ScopeSharedPtr scope1 = store.rootScope()->createScope("cluster.a");
  counters_.push_back(Stats::CounterSharedPtr(&scope1->counterFromStatName(name)));

  // To reproduce the problem from we will render
  // cluster.a.default.total_match_count before we discover the existence of
  // cluster.x.default.total_match_count. That will happen because "d" in
  // "default" comes before "x" with
  // https://github.com/envoyproxy/envoy/pull/24998
  Stats::ScopeSharedPtr scope2 = store.rootScope()->createScope("cluster.x");
  counters_.push_back(Stats::CounterSharedPtr(&scope2->counterFromStatName(name)));

  constexpr absl::string_view expected_output =
      R"EOF(# TYPE envoy_cluster_default_total_match_count counter
envoy_cluster_default_total_match_count{envoy_cluster_name="a"} 0
envoy_cluster_default_total_match_count{envoy_cluster_name="x"} 0
)EOF";

  // Note: in the version of prometheus_stats_test.cc that works with the
  // streaming GroupedStatsRequest, the test code is slightly different;
  // it's based on the local 'store' object rather than being based on
  // the counters_ member variable.
  //    StatsParams params;
  //    params.type_ = StatsType::Counters;
  //    params.format_ = StatsFormat::Prometheus;
  //    auto request = std::make_unique<GroupedStatsRequest>(store, params, custom_namespaces_);
  //    EXPECT_EQ(expected_output, response(*request));
  // This code is left here so that we can verify the bug is fixed if we decide to
  // re-try the streaming Prometheus implementation.

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(1, size);
  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithNonDefaultBuckets) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::ConstSupportedBuckets buckets{10, 20};
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(
      h1_cumulative.getHistogram(), Stats::Histogram::Unit::Unspecified, buckets);

  auto histogram = makeHistogram("histogram1", {});
  ON_CALL(*histogram, cumulativeStatistics()).WillByDefault(ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
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

// Test that scaled percents are emitted in the expected 0.0-1.0 range, and that the buckets
// apply to the final output range, not the internal scaled range.
TEST_F(PrometheusStatsFormatterTest, HistogramWithScaledPercent) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
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

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 1
envoy_histogram1_bucket{le="1"} 2
envoy_histogram1_bucket{le="+Inf"} 3
envoy_histogram1_sum{} 2.2599999999999997868371792719699
envoy_histogram1_count{} 3
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithHighCounts) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
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
  Stats::CustomStatNamespacesImpl custom_namespaces;
  custom_namespaces.registerStatNamespace("promtest");

  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addCounter("promtest.myapp.test.foo", {{makeStat("tag_name"), makeStat("tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});
  addGauge("promtest.MYAPP.test.bar", {{makeStat("tag_name"), makeStat("tag-value")}});
  // Metric with invalid prometheus namespace in the custom metric must be excluded in the output.
  addGauge("promtest.1234abcd.test.bar", {{makeStat("tag_name"), makeStat("tag-value")}});
  addClusterEndpoints("cluster1", 1, {{"a.tag-name", "a.tag-value"}});
  addClusterEndpoints("cluster2", 2, {{"another_tag_name", "another_tag-value"}});

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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(12UL, size);

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
# TYPE envoy_cluster_endpoint_c1 counter
envoy_cluster_endpoint_c1{a_tag_name="a.tag-value",envoy_cluster_name="cluster1",envoy_endpoint_address="127.0.0.1:80"} 11
envoy_cluster_endpoint_c1{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.2:80"} 21
envoy_cluster_endpoint_c1{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.3:80"} 31
# TYPE envoy_cluster_endpoint_c2 counter
envoy_cluster_endpoint_c2{a_tag_name="a.tag-value",envoy_cluster_name="cluster1",envoy_endpoint_address="127.0.0.1:80"} 12
envoy_cluster_endpoint_c2{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.2:80"} 22
envoy_cluster_endpoint_c2{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.3:80"} 32
# TYPE envoy_cluster_endpoint_g1 gauge
envoy_cluster_endpoint_g1{a_tag_name="a.tag-value",envoy_cluster_name="cluster1",envoy_endpoint_address="127.0.0.1:80"} 13
envoy_cluster_endpoint_g1{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.2:80"} 23
envoy_cluster_endpoint_g1{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.3:80"} 33
# TYPE envoy_cluster_endpoint_g2 gauge
envoy_cluster_endpoint_g2{a_tag_name="a.tag-value",envoy_cluster_name="cluster1",envoy_endpoint_address="127.0.0.1:80"} 14
envoy_cluster_endpoint_g2{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.2:80"} 24
envoy_cluster_endpoint_g2{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.3:80"} 34
# TYPE envoy_cluster_endpoint_healthy gauge
envoy_cluster_endpoint_healthy{a_tag_name="a.tag-value",envoy_cluster_name="cluster1",envoy_endpoint_address="127.0.0.1:80"} 1
envoy_cluster_endpoint_healthy{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.2:80"} 1
envoy_cluster_endpoint_healthy{another_tag_name="another_tag-value",envoy_cluster_name="cluster2",envoy_endpoint_address="127.0.0.3:80"} 1
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithTextReadoutsInGaugeFormat) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addCounter("cluster.upstream_cx_total_count", {{makeStat("cluster"), makeStat("c1")}});
  addGauge("cluster.upstream_cx_total", {{makeStat("cluster"), makeStat("c1")}});
  // Text readouts that should be returned in gauge format.
  addTextReadout("control_plane.identifier", "CP-1", {{makeStat("cluster"), makeStat("c1")}});
  addTextReadout("invalid_tag_values", "test",
                 {{makeStat("tag1"), makeStat(R"(\)")},
                  {makeStat("tag2"), makeStat("\n")},
                  {makeStat("tag3"), makeStat(R"(")")}});

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(4UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_upstream_cx_total_count counter
envoy_cluster_upstream_cx_total_count{cluster="c1"} 0
# TYPE envoy_cluster_upstream_cx_total gauge
envoy_cluster_upstream_cx_total{cluster="c1"} 0
# TYPE envoy_control_plane_identifier gauge
envoy_control_plane_identifier{cluster="c1",text_value="CP-1"} 0
# TYPE envoy_invalid_tag_values gauge
envoy_invalid_tag_values{tag1="\\",tag2="\n",tag3="\"",text_value="test"} 0
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

// Test that output groups all metrics of the same name (with different tags) together,
// as required by the Prometheus exposition format spec. Additionally, groups of metrics
// should be sorted by their tags; the format specifies that it is preferred that metrics
// are always grouped in the same order, and sorting is an easy way to ensure this.
TEST_F(PrometheusStatsFormatterTest, OutputSortedByMetricName) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
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
  Stats::CustomStatNamespacesImpl custom_namespaces;
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
  StatsParams params;
  params.used_only_ = true;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
      custom_namespaces);
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

TEST_F(PrometheusStatsFormatterTest, OutputWithHiddenGauge) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}},
           Stats::Gauge::ImportMode::HiddenAccumulate);

  StatsParams params;

  {
    Buffer::OwnedImpl response;
    params.hidden_ = HiddenFlag::Exclude;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    const std::string expected_output =
        R"EOF(# TYPE envoy_cluster_test_cluster_2_upstream_cx_total gauge
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
)EOF";
    EXPECT_EQ(expected_output, response.toString());
    EXPECT_EQ(1UL, size);
  }
  {
    Buffer::OwnedImpl response;
    params.hidden_ = HiddenFlag::ShowOnly;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    const std::string expected_output =
        R"EOF(# TYPE envoy_cluster_test_cluster_2_upstream_cx_total gauge
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
)EOF";
    EXPECT_EQ(expected_output, response.toString());
    EXPECT_EQ(1UL, size);
  }
  {
    Buffer::OwnedImpl response;
    params.hidden_ = HiddenFlag::Include;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    const std::string expected_output =
        R"EOF(# TYPE envoy_cluster_test_cluster_2_upstream_cx_total gauge
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
envoy_cluster_test_cluster_2_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
)EOF";
    EXPECT_EQ(expected_output, response.toString());
    EXPECT_EQ(1UL, size);
  }
}

TEST_F(PrometheusStatsFormatterTest, OutputWithUsedOnlyHistogram) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
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
  StatsParams params;

  {
    params.used_only_ = true;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).Times(0);

    Buffer::OwnedImpl response;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    EXPECT_EQ(0UL, size);
  }

  {
    params.used_only_ = false;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

    Buffer::OwnedImpl response;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    EXPECT_EQ(1UL, size);
  }
}

TEST_F(PrometheusStatsFormatterTest, OutputWithRegexp) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  addCounter("cluster.test_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_2.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_3.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_4.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});
  addClusterEndpoints("test_1", 1, {{"a.tag-name", "a.tag-value"}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("key1"), makeStat("value1")},
                                                        {makeStat("key2"), makeStat("value2")}});
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram1);

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
)EOF";
  {
    Buffer::OwnedImpl response;
    StatsParams params;
    ASSERT_EQ(Http::Code::OK,
              params.parse("/stats?filter=cluster.test_1.upstream_cx_total", response));
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    EXPECT_EQ(1UL, size);
    EXPECT_EQ(expected_output, response.toString());
  }

  {
    Buffer::OwnedImpl response;
    StatsParams params;
    ASSERT_EQ(Http::Code::OK,
              params.parse("/stats?filter=cluster.test_1.upstream_cx_total&safe", response));
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    EXPECT_EQ(1UL, size);
    EXPECT_EQ(expected_output, response.toString());
  }

  // Look for per-endpoint stats via filter
  {
    Buffer::OwnedImpl response;
    StatsParams params;
    ASSERT_EQ(Http::Code::OK, params.parse("/stats?filter=cluster.test_1.endpoint.*c1", response));
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheus(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    const std::string expected =
        R"EOF(# TYPE envoy_cluster_endpoint_c1 counter
envoy_cluster_endpoint_c1{a_tag_name="a.tag-value",envoy_cluster_name="test_1",envoy_endpoint_address="127.0.0.1:80"} 11
)EOF";
    EXPECT_EQ(1UL, size);
    EXPECT_EQ(expected, response.toString());
  }
}

} // namespace Server
} // namespace Envoy
