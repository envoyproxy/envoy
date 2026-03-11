#include <cmath>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/prometheus_stats.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/stats_utility.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Ref;
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
  //`statsAsPrometheusText()` should return two implying it found two unique stat names

  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStat("a.tag-name"), makeStat("a.tag-value")}});
  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{makeStat("another_tag_name"), makeStat("another_tag-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_3"), makeStat("another_tag_3-value")}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{makeStat("another_tag_name_4"), makeStat("another_tag_4-value")}});

  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(2UL, size);
}

TEST_F(PrometheusStatsFormatterTest, UniqueMetricName) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  // Create two counters and two gauges, all with unique names.
  // statsAsPrometheusText() should return four implying it found
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 1
envoy_histogram1_bucket{le="1"} 2
envoy_histogram1_bucket{le="+Inf"} 3
envoy_histogram1_sum{} 2.2578688482015323302221077028662
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response,
      StatsParams(), custom_namespaces);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 0
envoy_histogram1_bucket{le="1"} 100000
envoy_histogram1_bucket{le="5"} 100000
envoy_histogram1_bucket{le="10"} 100000
envoy_histogram1_bucket{le="25"} 100000
envoy_histogram1_bucket{le="50"} 100000
envoy_histogram1_bucket{le="100"} 1100000
envoy_histogram1_bucket{le="250"} 1100000
envoy_histogram1_bucket{le="500"} 1100000
envoy_histogram1_bucket{le="1000"} 101100000
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
envoy_histogram1_sum{} 104866771428.571441650390625
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 3
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 5
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5531.1160155998386471765115857124
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="50"} 3
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="100"} 5
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="5000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="aaa",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="aaa"} 5531.1160155998386471765115857124
envoy_cluster_upstream_response_time_count{cluster="aaa"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="0.5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="10"} 0
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="25"} 1
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="50"} 3
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="100"} 5
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="5000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="bbb",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="bbb"} 5531.1160155998386471765115857124
envoy_cluster_upstream_response_time_count{cluster="bbb"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="0.5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="5"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="10"} 0
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="25"} 1
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="50"} 3
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="100"} 5
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="250"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1000"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="2500"} 6
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="5000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="10000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="30000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="60000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="300000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="1800000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="3600000"} 7
envoy_cluster_upstream_response_time_bucket{cluster="ccc",le="+Inf"} 7
envoy_cluster_upstream_response_time_sum{cluster="ccc"} 5531.1160155998386471765115857124
envoy_cluster_upstream_response_time_count{cluster="ccc"} 7
# TYPE envoy_cluster_upstream_rq_time histogram
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="50"} 3
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="100"} 5
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="5000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="aaa",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="aaa"} 5531.1160155998386471765115857124
envoy_cluster_upstream_rq_time_count{cluster="aaa"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="50"} 3
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="100"} 5
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="5000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="bbb",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="bbb"} 5531.1160155998386471765115857124
envoy_cluster_upstream_rq_time_count{cluster="bbb"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="0.5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="5"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="10"} 0
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="25"} 1
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="50"} 3
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="100"} 5
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="250"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1000"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="2500"} 6
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="5000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="10000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="30000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="60000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="300000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="1800000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="3600000"} 7
envoy_cluster_upstream_rq_time_bucket{cluster="ccc",le="+Inf"} 7
envoy_cluster_upstream_rq_time_sum{cluster="ccc"} 5531.1160155998386471765115857124
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
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
      custom_namespaces);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 1
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 3
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 5
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5531.1160155998386471765115857124
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
        counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response, params,
        custom_namespaces);
    EXPECT_EQ(0UL, size);
  }

  {
    params.used_only_ = false;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

    Buffer::OwnedImpl response;
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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
    const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusText(
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

struct DecodedNativeHistogram {
  // A decoded bucket with its index and bounds.
  struct DecodedBucket {
    int32_t index;
    double lower_bound;
    double upper_bound;
    uint64_t count;
  };

  std::vector<DecodedBucket> positive_buckets;

  explicit DecodedNativeHistogram(const io::prometheus::client::Histogram& hist) {
    const double base = std::pow(2.0, std::pow(2.0, -hist.schema()));

    // Decode spans and deltas into buckets
    int32_t current_index = 0;
    int64_t current_count = 0;
    int delta_idx = 0;

    for (int span_idx = 0; span_idx < hist.positive_span_size(); ++span_idx) {
      const auto& span = hist.positive_span(span_idx);
      current_index += span.offset();

      for (uint32_t i = 0; i < span.length(); ++i) {
        current_count += hist.positive_delta(delta_idx);
        EXPECT_GE(current_count, 0);
        if (current_count > 0) {
          DecodedBucket bucket;
          bucket.index = current_index;
          bucket.lower_bound = std::pow(base, current_index);
          bucket.upper_bound = std::pow(base, current_index + 1);
          bucket.count = static_cast<uint64_t>(current_count);
          positive_buckets.push_back(bucket);
        }
        current_index++;
        delta_idx++;
      }
    }
  }

  // Helper to get total count from positive buckets.
  uint64_t totalPositiveBucketCount() const {
    uint64_t total = 0;
    for (const auto& bucket : positive_buckets) {
      total += bucket.count;
    }
    return total;
  }

  // Helper to get just the counts from positive buckets.
  std::vector<uint64_t> positiveCountsOnly() const {
    std::vector<uint64_t> counts;
    counts.reserve(positive_buckets.size());
    for (const auto& bucket : positive_buckets) {
      counts.push_back(bucket.count);
    }
    return counts;
  }

  // Compute the expected bucket index for a value given a schema.
  // Bucket i covers (base^i, base^(i+1)] where base = 2^(2^(-schema)).
  static int32_t expectedBucketIndex(int8_t schema, double value) {
    EXPECT_GT(value, 0) << "Only positive values have bucket indices";
    const double base = std::pow(2.0, std::pow(2.0, -schema));
    // For value v in (base^i, base^(i+1)], the bucket index is i.
    // Formula: i = ceil(log(v) / log(base)) - 1
    return static_cast<int32_t>(std::ceil(std::log(value) / std::log(base))) - 1;
  }
};

// Protobuf Format Tests

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithCountersAndGauges) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addCounter("cluster.test_1.upstream_cx_total", {{makeStat("cluster_name"), makeStat("test_1")}});
  addCounter("cluster.test_2.upstream_cx_total", {{makeStat("cluster_name"), makeStat("test_2")}});
  addGauge("cluster.test_1.upstream_cx_active", {{makeStat("cluster_name"), makeStat("test_1")}});

  counters_[0]->add(10);
  counters_[1]->add(20);
  gauges_[0]->set(5);

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);
  EXPECT_EQ(3UL, size);

  EXPECT_EQ("application/vnd.google.protobuf; "
            "proto=io.prometheus.client.MetricFamily; encoding=delimited",
            response_headers.getContentTypeValue());

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(3, families.size());

  EXPECT_EQ("envoy_cluster_test_1_upstream_cx_total", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::COUNTER, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());
  EXPECT_EQ(10, families[0].metric(0).counter().value());
  ASSERT_EQ(1, families[0].metric(0).label_size());
  EXPECT_EQ("cluster_name", families[0].metric(0).label(0).name());
  EXPECT_EQ("test_1", families[0].metric(0).label(0).value());

  EXPECT_EQ("envoy_cluster_test_2_upstream_cx_total", families[1].name());
  EXPECT_EQ(io::prometheus::client::MetricType::COUNTER, families[1].type());
  ASSERT_EQ(1, families[1].metric_size());
  EXPECT_EQ(20, families[1].metric(0).counter().value());

  EXPECT_EQ("envoy_cluster_test_1_upstream_cx_active", families[2].name());
  EXPECT_EQ(io::prometheus::client::MetricType::GAUGE, families[2].type());
  ASSERT_EQ(1, families[2].metric_size());
  EXPECT_EQ(5, families[2].metric(0).gauge().value());
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithHistogram) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("cluster"), makeStat("test_1")}});
  histogram->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram);
  EXPECT_CALL(*histogram, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  EXPECT_EQ("envoy_cluster_test_1_upstream_rq_time", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& metric = families[0].metric(0);
  ASSERT_EQ(1, metric.label_size());
  EXPECT_EQ("cluster", metric.label(0).name());
  EXPECT_EQ("test_1", metric.label(0).value());

  // Verify histogram data
  const auto& hist = metric.histogram();
  EXPECT_EQ(6, hist.sample_count());
  EXPECT_GT(hist.sample_sum(), 0);

  // Verify exact buckets match the supported buckets from the histogram statistics.
  Stats::ConstSupportedBuckets& supported_buckets = h1_cumulative_statistics.supportedBuckets();
  const std::vector<uint64_t>& computed_buckets = h1_cumulative_statistics.computedBuckets();
  EXPECT_EQ(supported_buckets.size(), hist.bucket_size());
  for (size_t i = 0; i < supported_buckets.size(); ++i) {
    EXPECT_EQ(supported_buckets[i], hist.bucket(i).upper_bound());
    EXPECT_EQ(computed_buckets[i], hist.bucket(i).cumulative_count());
  }

  // Verify +Inf bucket
  EXPECT_EQ(6, hist.bucket(hist.bucket_size() - 1).cumulative_count());
}

// Test protobuf traditional histogram with Percent unit to verify proper scaling.
// Values are stored internally with PercentScale applied, but output should be in 0-1 range.
TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithHistogramPercent) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  HistogramWrapper h1_cumulative;
  Stats::ConstSupportedBuckets buckets{0.5, 1.0};

  // Record 3 percent values: 25%, 75%, 125% (stored as scaled integers)
  constexpr double scale_factor = Stats::Histogram::PercentScale;
  h1_cumulative.setHistogramValuesWithCounts(std::vector<std::pair<uint64_t, uint64_t>>({
      {static_cast<uint64_t>(0.25 * scale_factor), 1},
      {static_cast<uint64_t>(0.75 * scale_factor), 1},
      {static_cast<uint64_t>(1.25 * scale_factor), 1},
  }));

  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram(),
                                                          Stats::Histogram::Unit::Percent, buckets);

  auto histogram = makeHistogram("percent_histogram", {{makeStat("cluster"), makeStat("test_1")}});
  histogram->unit_ = Stats::Histogram::Unit::Percent;
  addHistogram(histogram);
  EXPECT_CALL(*histogram, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  EXPECT_EQ("envoy_percent_histogram", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& hist = families[0].metric(0).histogram();
  EXPECT_EQ(3, hist.sample_count());

  // Verify sum is in 0-1 scale (should be ~2.25, not in millions)
  EXPECT_GT(hist.sample_sum(), 2.0);
  EXPECT_LT(hist.sample_sum(), 3.0);

  // Verify bucket bounds are in 0-1 scale
  ASSERT_EQ(2, hist.bucket_size());
  EXPECT_EQ(0.5, hist.bucket(0).upper_bound());
  EXPECT_EQ(1, hist.bucket(0).cumulative_count()); // 25% < 50%

  EXPECT_EQ(1.0, hist.bucket(1).upper_bound());
  EXPECT_EQ(2, hist.bucket(1).cumulative_count()); // 25% and 75% < 100%
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithSummary) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100};
  HistogramWrapper h1_interval;
  h1_interval.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_interval_statistics(h1_interval.getHistogram());

  auto histogram =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("cluster"), makeStat("test_1")}});
  addHistogram(histogram);
  EXPECT_CALL(*histogram, intervalStatistics()).WillOnce(ReturnRef(h1_interval_statistics));

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::Summary;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  EXPECT_EQ("envoy_cluster_test_1_upstream_rq_time", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::SUMMARY, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& metric = families[0].metric(0);
  const auto& summary = metric.summary();
  EXPECT_EQ(5, summary.sample_count());
  EXPECT_GT(summary.sample_sum(), 0);

  // Verify exact quantiles match the supported quantiles from the histogram statistics.
  Stats::ConstSupportedBuckets& supported_quantiles = h1_interval_statistics.supportedQuantiles();
  const std::vector<double>& computed_quantiles = h1_interval_statistics.computedQuantiles();
  EXPECT_EQ(supported_quantiles.size(), summary.quantile_size());
  for (size_t i = 0; i < supported_quantiles.size(); ++i) {
    EXPECT_EQ(supported_quantiles[i], summary.quantile(i).quantile());
    EXPECT_EQ(computed_quantiles[i], summary.quantile(i).value());
  }
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithTextReadouts) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addTextReadout("control_plane.identifier", "CP-1", {{makeStat("cluster"), makeStat("c1")}});
  addTextReadout("version", "1.2.3", {{makeStat("env"), makeStat("prod")}});

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);
  EXPECT_EQ(2UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(2, families.size());

  EXPECT_EQ("envoy_control_plane_identifier", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::GAUGE, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());
  const auto& metric1 = families[0].metric(0);
  EXPECT_EQ(0, metric1.gauge().value());

  // Should have cluster label + text_value label
  ASSERT_EQ(2, metric1.label_size());
  bool found_text_value = false;
  for (int i = 0; i < metric1.label_size(); i++) {
    if (metric1.label(i).name() == "text_value") {
      EXPECT_EQ("CP-1", metric1.label(i).value());
      found_text_value = true;
    }
  }
  EXPECT_TRUE(found_text_value);

  // Second text readout
  EXPECT_EQ("envoy_version", families[1].name());
  EXPECT_EQ(io::prometheus::client::MetricType::GAUGE, families[1].type());
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithMultipleTags) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addCounter("http.ingress.downstream_rq_total",
             {{makeStat("envoy_http_conn_manager_prefix"), makeStat("ingress")},
              {makeStat("envoy_response_code_class"), makeStat("2xx")}});

  counters_[0]->add(42);

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& metric = families[0].metric(0);
  ASSERT_EQ(2, metric.label_size());

  // Validate label contents - should have envoy_http_conn_manager_prefix and
  // envoy_response_code_class
  bool found_prefix = false;
  bool found_code_class = false;
  for (int i = 0; i < metric.label_size(); ++i) {
    if (metric.label(i).name() == "envoy_http_conn_manager_prefix") {
      EXPECT_EQ("ingress", metric.label(i).value());
      found_prefix = true;
    } else if (metric.label(i).name() == "envoy_response_code_class") {
      EXPECT_EQ("2xx", metric.label(i).value());
      found_code_class = true;
    }
  }
  EXPECT_TRUE(found_prefix);
  EXPECT_TRUE(found_code_class);

  EXPECT_EQ(42, metric.counter().value());
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithClusterEndpoints) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  addClusterEndpoints("cluster1", 1, {{"region", "us-east"}});
  addClusterEndpoints("cluster2", 2, {{"region", "us-west"}});

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, StatsParams(), custom_namespaces);
  EXPECT_EQ(5UL, size);

  auto prom_families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(5, prom_families.size());

  std::map<std::string, io::prometheus::client::MetricType> expected_families = {
      {"envoy_cluster_endpoint_c1", io::prometheus::client::MetricType::COUNTER},
      {"envoy_cluster_endpoint_c2", io::prometheus::client::MetricType::COUNTER},
      {"envoy_cluster_endpoint_g1", io::prometheus::client::MetricType::GAUGE},
      {"envoy_cluster_endpoint_g2", io::prometheus::client::MetricType::GAUGE},
      {"envoy_cluster_endpoint_healthy", io::prometheus::client::MetricType::GAUGE},
  };

  for (const auto& prom_family : prom_families) {
    auto it = expected_families.find(prom_family.name());
    ASSERT_NE(it, expected_families.end()) << "Unexpected metric family: " << prom_family.name();
    EXPECT_EQ(it->second, prom_family.type()) << "Wrong type for: " << prom_family.name();

    ASSERT_EQ(3, prom_family.metric_size()) << "Wrong metric count for: " << prom_family.name();

    for (int i = 0; i < prom_family.metric_size(); ++i) {
      const auto& metric = prom_family.metric(i);

      EXPECT_GE(metric.label_size(), 3)
          << "Metric " << i << " in " << prom_family.name() << " should have at least 3 labels";

      bool found_cluster_name = false;
      bool found_endpoint_address = false;
      bool found_region = false;
      for (int j = 0; j < metric.label_size(); ++j) {
        if (metric.label(j).name() == "envoy_cluster_name") {
          found_cluster_name = true;
          EXPECT_TRUE(metric.label(j).value() == "cluster1" ||
                      metric.label(j).value() == "cluster2");
        } else if (metric.label(j).name() == "envoy_endpoint_address") {
          found_endpoint_address = true;
          EXPECT_FALSE(metric.label(j).value().empty());
        } else if (metric.label(j).name() == "region") {
          found_region = true;
        }
      }
      EXPECT_TRUE(found_cluster_name) << "Missing envoy_cluster_name label";
      EXPECT_TRUE(found_endpoint_address) << "Missing envoy_endpoint_address label";
      EXPECT_TRUE(found_region) << "Missing region label";

      if (prom_family.type() == io::prometheus::client::MetricType::COUNTER) {
        EXPECT_GT(metric.counter().value(), 0);
      } else {
        EXPECT_GT(metric.gauge().value(), 0);
      }
    }
  }
}

// Test that protobuf is chosen when it is the first accept value.
TEST_F(PrometheusStatsFormatterTest, ContentNegotiationProtobufAcceptHeader) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  addCounter("test.counter", {});

  Http::TestRequestHeaderMapImpl request_headers{
      // This header value was copied from a real prometheus scraper request.
      {"accept", "application/"
                 "vnd.google.protobuf;proto=io.prometheus.client.MetricFamily;encoding=delimited;q="
                 "0.6,application/openmetrics-text;version=1.0.0;escaping=allow-utf-8;q=0.5,text/"
                 "plain;version=0.0.4;q=0.4,*/*;q=0.3"}};

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, request_headers,
      response_headers, response, StatsParams(), custom_namespaces);

  EXPECT_EQ("application/vnd.google.protobuf; proto=io.prometheus.client.MetricFamily; "
            "encoding=delimited",
            response_headers.getContentTypeValue());

  auto families = parsePrometheusProtobuf(response.toString());
  EXPECT_EQ(1, families.size());
}

// Test that text is chosen when it is the first accept value.
TEST_F(PrometheusStatsFormatterTest, ContentNegotiationTextPlainAcceptHeader) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  addCounter("test.counter", {});

  // Both text and protobuf are accepted, with text as a higher priority.
  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "text/plain;version=0.0.4;q=0.6,vnd.google.protobuf;proto=io.prometheus.client."
                 "MetricFamily;encoding=delimited;q=0.5,*/*;q=0.4"}};

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, request_headers,
      response_headers, response, StatsParams(), custom_namespaces);

  EXPECT_TRUE(response_headers.getContentTypeValue().empty());

  std::string output = response.toString();
  EXPECT_TRUE(output.find("# TYPE") != std::string::npos);
}

TEST_F(PrometheusStatsFormatterTest, ContentNegotiationDefaultToText) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  addCounter("test.counter", {});

  Http::TestRequestHeaderMapImpl request_headers;
  // No Accept header

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, request_headers,
      response_headers, response, StatsParams(), custom_namespaces);

  // Should default to text format
  std::string output = response.toString();
  EXPECT_TRUE(output.find("# TYPE") != std::string::npos);
}

TEST_F(PrometheusStatsFormatterTest, QueryParamOverridesAcceptHeader) {
  Stats::CustomStatNamespacesImpl custom_namespaces;
  addCounter("test.counter", {});

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "text/plain"}};

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;

  StatsParams params;
  Buffer::OwnedImpl parse_buffer;
  params.parse("?prom_protobuf=1", parse_buffer);

  PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, request_headers,
      response_headers, response, params, custom_namespaces);

  // Query param should override Accept header - should use protobuf
  EXPECT_EQ("application/vnd.google.protobuf; "
            "proto=io.prometheus.client.MetricFamily; encoding=delimited",
            response_headers.getContentTypeValue());

  auto families = parsePrometheusProtobuf(response.toString());
  EXPECT_EQ(1, families.size());
}

TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithNativeHistogram) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  // Create histogram with some values for cumulative statistics.
  // Include zeros to test zero bucket handling.
  const std::vector<uint64_t> h1_values = {0, 0, 0, 50, 20, 30, 70, 100, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("cluster"), makeStat("test_1")}});
  histogram->unit_ = Stats::Histogram::Unit::Milliseconds;
  addHistogram(histogram);

  // Set up detailed buckets that will be returned by detailedTotalBuckets().
  // These are used to determine the data range for schema selection.
  std::vector<Stats::ParentHistogram::Bucket> detailed_buckets = {
      {0.0, 0.1, 3},    // [0, 0.1): 3 zeros
      {10.0, 10.0, 2},  // [10, 20): 2 samples
      {50.0, 25.0, 3},  // [50, 75): 3 samples
      {100.0, 50.0, 1}, // [100, 150): 1 sample
  };

  EXPECT_CALL(*histogram, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));
  EXPECT_CALL(*histogram, detailedTotalBuckets()).WillOnce(testing::Return(detailed_buckets));

  EXPECT_CALL(*histogram, cumulativeCountLessThanOrEqualToValue(testing::_))
      .WillRepeatedly([](double value) -> uint64_t {
        if (value < 0.0) {
          return 0; // Nothing below 0
        }
        if (value < 10.0) {
          return 3; // 3 zeros at value 0 (samples at 0 are <= any value >= 0)
        }
        if (value < 50.0) {
          return 5; // 3 zeros + 2 samples at 10 (samples at 10 are <= any value >= 10)
        }
        if (value < 100.0) {
          return 8; // + 3 samples at 50
        }
        return 9; // + 1 sample at 100
      });

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  params.native_histogram_max_buckets_ = 20;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  EXPECT_EQ("envoy_cluster_test_1_upstream_rq_time", families[0].name());
  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& metric = families[0].metric(0);
  ASSERT_EQ(1, metric.label_size());
  EXPECT_EQ("cluster", metric.label(0).name());
  EXPECT_EQ("test_1", metric.label(0).value());

  const auto& hist = metric.histogram();

  // Verify basic histogram properties
  EXPECT_EQ(9, hist.sample_count());
  EXPECT_GT(hist.sample_sum(), 0);
  EXPECT_GE(hist.schema(), -4);
  EXPECT_LE(hist.schema(), 5);
  EXPECT_EQ(0.5, hist.zero_threshold());
  EXPECT_EQ(3, hist.zero_count());

  const DecodedNativeHistogram decoded(hist);

  ASSERT_EQ(3, decoded.positive_buckets.size());

  EXPECT_EQ(2, decoded.positive_buckets[0].count);
  EXPECT_EQ(DecodedNativeHistogram::expectedBucketIndex(hist.schema(), 10.0),
            decoded.positive_buckets[0].index);

  EXPECT_EQ(3, decoded.positive_buckets[1].count);
  EXPECT_EQ(DecodedNativeHistogram::expectedBucketIndex(hist.schema(), 50.0),
            decoded.positive_buckets[1].index);

  EXPECT_EQ(1, decoded.positive_buckets[2].count);
  EXPECT_EQ(DecodedNativeHistogram::expectedBucketIndex(hist.schema(), 100.0),
            decoded.positive_buckets[2].index);

  // Verify total: positive buckets + zero_count = sample_count
  EXPECT_EQ(hist.sample_count(), decoded.totalPositiveBucketCount() + hist.zero_count());

  // Should NOT have classic buckets (those are mutually exclusive with native)
  EXPECT_EQ(0, hist.bucket_size());
}

// Test that the special empty span is generated which the spec states is how an empty native
// histogram is distinguished from a traditional histogram.
TEST_F(PrometheusStatsFormatterTest, ProtobufOutputWithNativeHistogramEmptyBuckets) {
  Stats::CustomStatNamespacesImpl custom_namespaces;

  // Create histogram with zero samples
  HistogramWrapper h1_cumulative;
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram =
      makeHistogram("cluster.test_1.upstream_rq_time", {{makeStat("cluster"), makeStat("test_1")}});
  addHistogram(histogram);

  // Empty detailed buckets
  std::vector<Stats::ParentHistogram::Bucket> detailed_buckets = {};

  EXPECT_CALL(*histogram, cumulativeStatistics()).WillOnce(ReturnRef(h1_cumulative_statistics));

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters_, gauges_, histograms_, textReadouts_, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();
  const DecodedNativeHistogram decoded(hist);

  EXPECT_TRUE(hist.has_schema());
  EXPECT_EQ(0, hist.sample_count());
  EXPECT_EQ(0, hist.zero_count());
  EXPECT_TRUE(decoded.positive_buckets.empty());
}

// Test fixture for native histogram tests using real histogram implementation.
// This validates that the prometheus native histogram output works correctly
// with the actual circllhist implementation.
class RealHistogramNativePrometheusTest : public testing::Test {
public:
  RealHistogramNativePrometheusTest()
      : alloc_(*symbol_table_), store_(std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_)),
        scope_(*store_->rootScope()),
        endpoints_helper_(std::make_unique<Upstream::PerEndpointMetricsTestHelper>()) {
    store_->addSink(sink_);
    store_->initializeThreading(main_thread_dispatcher_, tls_);
  }

  ~RealHistogramNativePrometheusTest() override {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  // Configure custom histogram buckets for stats matching the given prefix.
  void setHistogramBucketsForPrefix(const std::string& prefix, const std::vector<double>& buckets) {
    envoy::config::metrics::v3::StatsConfig config;
    auto* bucket_settings = config.add_histogram_bucket_settings();
    bucket_settings->mutable_match()->set_prefix(prefix);
    for (double bucket : buckets) {
      bucket_settings->mutable_buckets()->Add(bucket);
    }
    store_->setHistogramSettings(
        std::make_unique<Stats::HistogramSettingsImpl>(config, factory_context_));
  }

  Stats::Histogram& makeHistogram(const std::string& name, Stats::Histogram::Unit unit) {
    return scope_.histogramFromString(name, unit);
  }

  void recordValue(Stats::Histogram& histogram, uint64_t value) {
    EXPECT_CALL(sink_, onHistogramComplete(Ref(histogram), value));
    histogram.recordValue(value);
  }

  void mergeHistograms() {
    store_->mergeHistograms([]() -> void {});
  }

  Stats::ParentHistogramSharedPtr getParentHistogram(const std::string& name) {
    for (const auto& histogram : store_->histograms()) {
      if (histogram->name().find(name) != std::string::npos) {
        return histogram;
      }
    }
    return nullptr;
  }

  // Decode delta-encoded positive bucket counts and return the total sample count.
  // In Prometheus native histograms, positive_delta values are delta-encoded:
  // delta[0] = count[0], delta[i] = count[i] - count[i-1] for i > 0.
  static int64_t decodeTotalCountFromBuckets(const io::prometheus::client::Histogram& hist) {
    // Envoy histograms record unsigned integers, so negative values are not possible.
    EXPECT_EQ(hist.negative_delta_size(), 0);

    int64_t running_count = 0;
    int64_t bucket_count = 0;
    for (int i = 0; i < hist.positive_delta_size(); ++i) {
      bucket_count += hist.positive_delta(i);
      EXPECT_GE(running_count, 0) << "Bucket " << i << " has negative count after delta decoding";
      running_count += bucket_count;
    }
    return running_count;
  }

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::AllocatorImpl alloc_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> store_;
  Stats::Scope& scope_;
  NiceMock<Stats::MockSink> sink_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
  std::unique_ptr<Upstream::PerEndpointMetricsTestHelper> endpoints_helper_;
};

// Test native histogram with only zero values using real histogram implementation.
// All samples should go to the zero bucket, with no positive buckets.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramWithOnlyZeros) {
  Stats::Histogram& h1 = makeHistogram("histogram_zeros", Stats::Histogram::Unit::Unspecified);

  // Record 5 zeros
  for (int i = 0; i < 5; ++i) {
    recordValue(h1, 0);
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_zeros");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify zero bucket contains all samples
  EXPECT_EQ(5, hist.sample_count());
  EXPECT_EQ(5, hist.zero_count());
  EXPECT_EQ(0.5, hist.zero_threshold());

  // No positive buckets should have data.
  EXPECT_EQ(0, hist.positive_delta_size());
  EXPECT_EQ(0, hist.positive_span_size());
}

// Test native histogram with mix of zeros and positive values using real histogram.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramWithZerosAndPositiveValues) {
  Stats::Histogram& h1 = makeHistogram("histogram_mixed", Stats::Histogram::Unit::Unspecified);

  for (int i = 0; i < 3; ++i) {
    recordValue(h1, 0);
  }
  for (int i = 0; i < 2; ++i) {
    recordValue(h1, 10);
  }
  for (int i = 0; i < 4; ++i) {
    recordValue(h1, 100);
  }

  for (int i = 0; i < 5; ++i) {
    recordValue(h1, 1ULL << 35);
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_mixed");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify counts
  EXPECT_EQ(14, hist.sample_count());
  EXPECT_EQ(3, hist.zero_count());
  EXPECT_EQ(0.5, hist.zero_threshold());

  // Positive buckets should have 11 samples (2 + 4 + 5).
  EXPECT_EQ(11, decodeTotalCountFromBuckets(hist));
}

// Test native histogram with bucket starting at 1 (just above zero threshold).
// This validates correct handling of values at the zero/positive boundary.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramWithBoundaryValues) {
  Stats::Histogram& h1 = makeHistogram("histogram_boundary", Stats::Histogram::Unit::Unspecified);

  // Record some zeros
  for (int i = 0; i < 2; ++i) {
    recordValue(h1, 0);
  }
  // Record values at 1 (above the zero_threshold of 0.5, so goes in positive buckets)
  for (int i = 0; i < 3; ++i) {
    recordValue(h1, 1);
  }
  // Record values at 2
  for (int i = 0; i < 2; ++i) {
    recordValue(h1, 2);
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_boundary");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify sample count
  EXPECT_EQ(7, hist.sample_count());

  // Zero bucket should have the 2 zeros
  EXPECT_EQ(2, hist.zero_count());
  EXPECT_EQ(0.5, hist.zero_threshold());

  // Positive buckets should have the remaining 5 samples (3 ones + 2 twos).
  EXPECT_EQ(5, decodeTotalCountFromBuckets(hist));

  // With values spanning only 1 to 2 (one doubling) and default max_buckets of 20,
  // schema 4 should be selected (16 buckets per doubling fits easily).
  EXPECT_EQ(4, hist.schema());

  const DecodedNativeHistogram decoded(hist);

  // At schema 4, values 1 and 2 are in different buckets:
  // - base = 2^(2^(-4)) = 2^(1/16) ≈ 1.044
  // - value 1.0: index = ceil(log(1) / log(base)) - 1 = -1
  // - value 2.0: Mathematically would be index 15, but base^16 = 2.0 exactly,
  //   so this is an exact bucket boundary. Due to how circllhist handles
  //   boundaries during interpolation, the samples end up in bucket 16.
  ASSERT_EQ(2, decoded.positive_buckets.size());

  // First bucket should contain value 1
  EXPECT_EQ(-1, decoded.positive_buckets[0].index);
  EXPECT_EQ(3, decoded.positive_buckets[0].count); // 3 ones
  EXPECT_LT(decoded.positive_buckets[0].lower_bound, 1.0);
  EXPECT_GE(decoded.positive_buckets[0].upper_bound, 1.0);

  // Second bucket should contain value 2
  // Note: index is 16 rather than 15 due to boundary handling at base^16 = 2.0.
  // Bucket 16 covers (base^16, base^17] = (2.0, ~2.088], but circllhist
  // interpolation places the samples here anyway.
  EXPECT_EQ(16, decoded.positive_buckets[1].index);
  EXPECT_EQ(2, decoded.positive_buckets[1].count); // 2 twos
  // Use EXPECT_NEAR due to floating-point precision in std::pow(base, 16)
  EXPECT_NEAR(2.0, decoded.positive_buckets[1].lower_bound, 1e-10);
  EXPECT_GT(decoded.positive_buckets[1].upper_bound, 2.0);
}

// Test native histogram with wide value range to exercise schema selection.
// With 6 sparse values and max_buckets=5, schema must be coarse enough to merge
// some values into shared buckets.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramWithWideRange) {
  Stats::Histogram& h1 = makeHistogram("histogram_wide", Stats::Histogram::Unit::Unspecified);

  // Record 6 sparse values spanning several orders of magnitude
  recordValue(h1, 1);
  recordValue(h1, 10);
  recordValue(h1, 100);
  recordValue(h1, 1000);
  recordValue(h1, 10000);
  recordValue(h1, 100000);
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_wide");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  params.native_histogram_max_buckets_ = 5;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify all samples counted
  EXPECT_EQ(6, hist.sample_count());
  EXPECT_EQ(0, hist.zero_count()); // No zeros recorded

  // With 6 sparse values and max_buckets=5, we need a schema coarse enough
  // to merge some values into shared buckets:
  // - Schema -2 (base=16): 6 distinct buckets for 1, 10, 100, 1000, 10000, 100000
  // - Schema -3 (base=256): 4 buckets - values 10&100 share one, 1000&10000 share one
  //   Bucket -1: (1/256, 1] contains 1
  //   Bucket 0: (1, 256] contains 10 and 100
  //   Bucket 1: (256, 65536] contains 1000 and 10000
  //   Bucket 2: (65536, 16M] contains 100000
  EXPECT_EQ(-3, hist.schema());

  // All samples should be in positive buckets.
  EXPECT_EQ(6, decodeTotalCountFromBuckets(hist));

  // Verify bucket structure: 4 buckets due to merging at schema -3
  const DecodedNativeHistogram decoded(hist);
  EXPECT_LE(decoded.positive_buckets.size(), 5); // Should fit in max_buckets
}

// Test native histogram with schema reduction due to max_buckets constraint.
// Values 1-100 span about 7 doublings. With max_buckets=10, this forces schema 0.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramSchemaReduction) {
  Stats::Histogram& h1 = makeHistogram("histogram_schema", Stats::Histogram::Unit::Unspecified);

  // Record values 1-100, spanning about 7 doublings (2^7 = 128)
  for (uint64_t v = 1; v <= 100; ++v) {
    recordValue(h1, v);
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_schema");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  // Use small max_buckets to force schema reduction
  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  params.native_histogram_max_buckets_ = 10;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify all samples counted
  EXPECT_EQ(100, hist.sample_count());

  // With max_buckets=10 and values 1-100 spanning ~7 doublings:
  // - Schema 4 (16 buckets/doubling) would need ~112 buckets
  // - Schema 3 (8 buckets/doubling) would need ~56 buckets
  // - Schema 2 (4 buckets/doubling) would need ~28 buckets
  // - Schema 1 (2 buckets/doubling) would need ~14 buckets
  // - Schema 0 (1 bucket/doubling) would need ~7 buckets (fits!)
  EXPECT_EQ(0, hist.schema());

  // All samples should be in positive buckets.
  EXPECT_EQ(100, decodeTotalCountFromBuckets(hist));

  // Should fit in max_buckets
  EXPECT_LE(hist.positive_delta_size(), 10);
}

// Test native histogram with percent values spanning below and above 100%.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramWithPercent) {
  Stats::Histogram& h1 = makeHistogram("histogram_percent_mixed", Stats::Histogram::Unit::Percent);

  // Record percent values spanning a wide range: 0% to 300%
  // These are stored internally with PercentScale applied.
  recordValue(h1, static_cast<uint64_t>(0));
  recordValue(h1, static_cast<uint64_t>(0.00001 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(0.01 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(0.10 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(0.50 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(0.75 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(1.00 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(1.50 * Stats::Histogram::PercentScale));
  recordValue(h1, static_cast<uint64_t>(3.00 * Stats::Histogram::PercentScale));
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_percent_mixed");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify all samples counted
  EXPECT_EQ(9, hist.sample_count());
  EXPECT_EQ(1, hist.zero_count()); // The 0 value

  // With PercentScale applied, the output bucket bounds should be in 0-3 range.
  // The 8 sparse non-zero values (0.00001 to 3.0) span about 18 doublings.
  // With default max_buckets=20, schema 2 should be selected (4 buckets per doubling).
  EXPECT_EQ(2, hist.schema());

  // Verify positive bucket total matches non-zero samples
  EXPECT_EQ(8, decodeTotalCountFromBuckets(hist));

  // With 8 sparse values spanning a wide range (0.00001 to 3.0), each value should
  // fall into a distinct native histogram bucket at schema 2.
  EXPECT_EQ(8, hist.positive_delta_size());

  const DecodedNativeHistogram decoded(hist);

  // Verify decoded bucket count matches positive_delta_size
  EXPECT_EQ(8, decoded.positive_buckets.size());

  // Verify each recorded non-zero value has a corresponding bucket with count 1.
  // Since values are sparse and sorted, they map 1:1 to consecutive decoded buckets.
  // At schema 2, bucket i covers (base^i, base^(i+1)].
  const std::vector<double> recorded_values = {0.00001, 0.01, 0.10, 0.50, 0.75, 1.00, 1.50, 3.00};
  for (size_t i = 0; i < recorded_values.size(); ++i) {
    const double value = recorded_values[i];
    const auto& bucket = decoded.positive_buckets[i];
    // Each bucket should have count 1
    EXPECT_EQ(1, bucket.count) << "Bucket " << i << " for value " << value;
    // Value should fall within bucket bounds (with floating point tolerance)
    EXPECT_LT(bucket.lower_bound, value * 1.01)
        << "Bucket " << i << " lower_bound too high for value " << value;
    EXPECT_GE(bucket.upper_bound, value * 0.99)
        << "Bucket " << i << " upper_bound too low for value " << value;

    // Verify bucket index matches expected calculation
    const int32_t expected_index =
        DecodedNativeHistogram::expectedBucketIndex(hist.schema(), value);
    EXPECT_EQ(expected_index, bucket.index)
        << "Bucket " << i << " index mismatch for value " << value;

    if (value > 1.0) {
      EXPECT_GE(bucket.index, 0);
    } else {
      EXPECT_LT(bucket.index, 0);
    }
  }
}

// Helper class for detailed native histogram bucket verification.
// Decodes the span/delta encoding to produce a map of bucket index -> count.
class NativeHistogramDecoder {
public:
  // Decode positive buckets from spans and deltas.
  // Returns a map of bucket_index -> count.
  static std::map<int32_t, uint64_t>
  decodePositiveBuckets(const io::prometheus::client::Histogram& hist) {
    std::map<int32_t, uint64_t> buckets;
    int32_t current_index = 0;
    int64_t current_count = 0;
    int delta_idx = 0;

    for (int span_idx = 0; span_idx < hist.positive_span_size(); ++span_idx) {
      const auto& span = hist.positive_span(span_idx);
      // For the first span, offset is absolute index.
      // For subsequent spans, offset is (gap - 1), and we're already one past
      // the last bucket from the previous span's loop increment, so just add offset.
      current_index += span.offset();

      for (uint32_t i = 0; i < span.length(); ++i) {
        current_count += hist.positive_delta(delta_idx);
        if (current_count > 0) {
          buckets[current_index] = static_cast<uint64_t>(current_count);
        }
        current_index++;
        delta_idx++;
      }
    }
    return buckets;
  }

  // Given a schema and value, compute the expected bucket index.
  // Bucket i covers (base^i, base^(i+1)] where base = 2^(2^(-schema)).
  static int32_t expectedBucketIndex(int8_t schema, double value) {
    EXPECT_GT(value, 0) << "Only positive values have bucket indices";
    double base = std::pow(2.0, std::pow(2.0, -schema));
    // For value v in (base^i, base^(i+1)], the bucket index is i.
    // At exact boundaries, v = base^(i+1) is in bucket i.
    // Formula: i = ceil(log(v) / log(base)) - 1
    double log_base = std::log(base);
    return static_cast<int32_t>(std::ceil(std::log(value) / log_base)) - 1;
  }

  // Compute the upper bound of a bucket given its index and schema.
  static double bucketUpperBound(int8_t schema, int32_t index) {
    double base = std::pow(2.0, std::pow(2.0, -schema));
    return std::pow(base, index + 1);
  }

  // Compute the lower bound of a bucket given its index and schema.
  static double bucketLowerBound(int8_t schema, int32_t index) {
    double base = std::pow(2.0, std::pow(2.0, -schema));
    return std::pow(base, index);
  }
};

// Test native histogram accuracy with sparse data.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramSparseDataAccuracy) {
  Stats::Histogram& h1 = makeHistogram("histogram_sparse", Stats::Histogram::Unit::Unspecified);

  struct ValueCount {
    uint64_t value;
    uint64_t count;
  };
  const std::vector<ValueCount> recorded_values = {
      {1, 2},       // 2 samples at 1
      {1000, 3},    // 3 samples at 1000
      {1000000, 1}, // 1 sample at 1000000
  };

  uint64_t total_count = 0;
  for (const auto& vc : recorded_values) {
    for (uint64_t i = 0; i < vc.count; ++i) {
      recordValue(h1, vc.value);
    }
    total_count += vc.count;
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_sparse");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                                      endpoints_helper_->cm_, response_headers,
                                                      response, params, custom_namespaces_);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify total counts
  EXPECT_EQ(total_count, hist.sample_count());
  EXPECT_EQ(0, hist.zero_count());

  const DecodedNativeHistogram decoded(hist);
  const int8_t schema = static_cast<int8_t>(hist.schema());

  // Should have exactly 3 buckets (one for each distinct value, since values are far apart)
  ASSERT_EQ(recorded_values.size(), decoded.positive_buckets.size())
      << "Sparse data should produce one bucket per distinct value";

  // Verify total bucket count matches sample count
  EXPECT_EQ(total_count, decoded.totalPositiveBucketCount());

  // Verify each recorded value is in a bucket with the correct count and bounds.
  // Since values are sorted and far apart, decoded buckets should be in the same order.
  for (size_t i = 0; i < recorded_values.size(); ++i) {
    const auto& vc = recorded_values[i];
    const auto& bucket = decoded.positive_buckets[i];
    const double value = static_cast<double>(vc.value);

    // Verify this bucket has the expected count
    EXPECT_EQ(vc.count, bucket.count)
        << "Bucket " << i << " for value " << vc.value << " should have count " << vc.count;

    // Verify the bucket bounds contain the value (bucket covers (lower, upper])
    EXPECT_LT(bucket.lower_bound, value * 1.01)
        << "Bucket " << i << " lower bound should be below value " << vc.value;
    EXPECT_GE(bucket.upper_bound, value * 0.99)
        << "Bucket " << i << " upper bound should be at or above value " << vc.value;

    // Verify the bucket index is correct for this value
    const int32_t expected_idx = DecodedNativeHistogram::expectedBucketIndex(schema, value);
    EXPECT_EQ(expected_idx, bucket.index)
        << "Bucket " << i << " should have expected index for value " << vc.value;
  }

  // Verify there are gaps between bucket indices (sparse data characteristic)
  for (size_t i = 1; i < decoded.positive_buckets.size(); ++i) {
    const int32_t gap = decoded.positive_buckets[i].index - decoded.positive_buckets[i - 1].index;
    EXPECT_GT(gap, 1) << "Sparse values should have gaps between bucket indices";
  }
}

// Test native histogram accuracy with dense data (consecutive values).
// All samples should be accounted for in adjacent buckets.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramDenseDataAccuracy) {
  Stats::Histogram& h1 = makeHistogram("histogram_dense", Stats::Histogram::Unit::Unspecified);

  std::map<uint64_t, int> value_counts;
  for (uint64_t v = 10; v <= 20; ++v) {
    recordValue(h1, v);
    value_counts[v]++;
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_dense");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                                      endpoints_helper_->cm_, response_headers,
                                                      response, params, custom_namespaces_);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify counts
  EXPECT_EQ(11, hist.sample_count()); // 10, 11, 12, ..., 20 = 11 values
  EXPECT_EQ(0, hist.zero_count());

  // Decode buckets
  auto buckets = NativeHistogramDecoder::decodePositiveBuckets(hist);
  int8_t schema = static_cast<int8_t>(hist.schema());

  // Verify total count from buckets matches sample count
  uint64_t total_from_buckets = 0;
  for (const auto& [idx, count] : buckets) {
    total_from_buckets += count;
  }
  EXPECT_EQ(11, total_from_buckets);

  // Verify each value falls within its bucket's bounds
  for (uint64_t v = 10; v <= 20; ++v) {
    int32_t expected_idx =
        NativeHistogramDecoder::expectedBucketIndex(schema, static_cast<double>(v));

    // The bucket should exist
    EXPECT_TRUE(buckets.count(expected_idx) > 0 || buckets.count(expected_idx - 1) > 0 ||
                buckets.count(expected_idx + 1) > 0)
        << "Value " << v << " should be in bucket near index " << expected_idx;
  }

  // With dense data spanning only 2x range (10-20), buckets should be relatively contiguous.
  // At schema 5, there are ~32 buckets per doubling, so 10-20 spans roughly one doubling.
  int32_t min_idx = buckets.begin()->first;
  int32_t max_idx = buckets.rbegin()->first;
  EXPECT_LE(max_idx - min_idx, 40) << "Dense data should have relatively few bucket gaps";
}

// Test that bucket indices are mathematically correct for known values.
// Uses a high max_buckets to ensure schema 4 (the finest available), allowing
// us to pre-calculate exact expected bucket indices.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramBucketIndexAccuracy) {
  Stats::Histogram& h1 = makeHistogram("histogram_exact", Stats::Histogram::Unit::Unspecified);

  // At schema 4, base = 2^(2^(-4)) = 2^(1/16) ≈ 1.044274
  // Bucket i covers (base^i, base^(i+1)]
  // For value v, bucket index = ceil(log(v)/log(base)) - 1
  constexpr int8_t expected_schema = 4;
  const double base = std::pow(2.0, std::pow(2.0, -expected_schema)); // 2^(1/16)

  // Pre-calculate expected bucket indices for powers of 2
  // At schema 4, there are 16 buckets per doubling (2^(1/16)^16 = 2)
  struct ValueExpectation {
    uint64_t value;
    int32_t expected_index;
  };

  std::vector<ValueExpectation> test_values;
  for (uint64_t v : {2, 4, 8, 16, 32}) {
    // Bucket index formula: ceil(log(v)/log(base)) - 1
    const int32_t idx =
        static_cast<int32_t>(std::ceil(std::log(static_cast<double>(v)) / std::log(base))) - 1;
    test_values.push_back({v, idx});
    recordValue(h1, v);
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_exact");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  // Use high max_buckets to ensure we get schema 4 (the finest)
  params.native_histogram_max_buckets_ = 100;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                                      endpoints_helper_->cm_, response_headers,
                                                      response, params, custom_namespaces_);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify we got the expected schema
  ASSERT_EQ(expected_schema, hist.schema()) << "Should use schema 4 with high max_buckets";

  // Verify sample counts
  EXPECT_EQ(test_values.size(), hist.sample_count());
  EXPECT_EQ(0, hist.zero_count());

  // Decode and verify each value landed in its expected bucket
  const DecodedNativeHistogram decoded(hist);

  // Should have exactly 5 buckets (one per value, since powers of 2 are far apart at schema 4)
  ASSERT_EQ(test_values.size(), decoded.positive_buckets.size());

  for (size_t i = 0; i < test_values.size(); ++i) {
    const auto& expected = test_values[i];
    const auto& bucket = decoded.positive_buckets[i];

    EXPECT_EQ(expected.expected_index, bucket.index)
        << "Value " << expected.value << " should be in bucket " << expected.expected_index;
    EXPECT_EQ(1, bucket.count) << "Bucket for value " << expected.value << " should have count 1";

    // Verify bounds: bucket covers (base^idx, base^(idx+1)]
    const double expected_lower = std::pow(base, expected.expected_index);
    const double expected_upper = std::pow(base, expected.expected_index + 1);
    EXPECT_NEAR(expected_lower, bucket.lower_bound, expected_lower * 1e-10);
    EXPECT_NEAR(expected_upper, bucket.upper_bound, expected_upper * 1e-10);

    // Value should be within bucket bounds
    const double value = static_cast<double>(expected.value);
    EXPECT_LT(bucket.lower_bound, value);
    EXPECT_GE(bucket.upper_bound, value);
  }
}

TEST_F(RealHistogramNativePrometheusTest, NativeHistogramVerySparseData) {
  Stats::Histogram& h1 =
      makeHistogram("histogram_very_sparse", Stats::Histogram::Unit::Unspecified);

  // Record extremely sparse values: 1 and 2^60
  // These are 60 doublings apart in log2 space
  recordValue(h1, 1);
  recordValue(h1, 1ULL << 60);
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_very_sparse");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                                      endpoints_helper_->cm_, response_headers,
                                                      response, params, custom_namespaces_);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // With only 2 sparse values, schema 4 (the finest/default) should be used
  EXPECT_EQ(4, hist.schema());

  // Verify all samples counted
  EXPECT_EQ(2, hist.sample_count());
  EXPECT_EQ(0, hist.zero_count());

  // Decode buckets
  const DecodedNativeHistogram decoded(hist);

  // Should have exactly 2 buckets (one for each value)
  ASSERT_EQ(2, decoded.positive_buckets.size())
      << "Very sparse data should result in exactly 2 buckets";

  // Each bucket should have count 1
  EXPECT_EQ(1, decoded.positive_buckets[0].count);
  EXPECT_EQ(1, decoded.positive_buckets[1].count);

  // Verify total matches
  EXPECT_EQ(2, decoded.totalPositiveBucketCount());

  // The bucket indices should be far apart (60 doublings * 16 buckets/doubling at schema 4 = 960)
  const int32_t index_gap = decoded.positive_buckets[1].index - decoded.positive_buckets[0].index;
  EXPECT_GT(index_gap, 900) << "Buckets for 1 and 2^60 should be ~960 indices apart at schema 4";
}

// Test traditional histogram in protobuf format with Percent unit.
// Verifies that percent values are properly scaled in the output (0-1 range, not raw scaled
// values). Uses percent-appropriate bucket configuration.
TEST_F(RealHistogramNativePrometheusTest, TraditionalHistogramWithPercent) {
  // Configure percent-appropriate buckets (in 0-1 scale)
  const std::vector<double> percent_buckets = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
  setHistogramBucketsForPrefix("histogram_percent", percent_buckets);

  Stats::Histogram& h1 = makeHistogram("histogram_percent", Stats::Histogram::Unit::Percent);

  // Record percent values spanning 0% to 150%.
  // Values are stored internally with PercentScale applied.
  const std::vector<double> percent_values = {0.0, 0.25, 0.50, 0.75, 1.00, 1.25, 1.50};
  constexpr double scale_factor = Stats::Histogram::PercentScale;

  double expected_sum = 0.0;
  for (double pct : percent_values) {
    recordValue(h1, static_cast<uint64_t>(pct * scale_factor));
    expected_sum += pct;
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_percent");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  // Use default params (traditional/cumulative histogram, not native)
  StatsParams params;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, families[0].type());
  ASSERT_EQ(1, families[0].metric_size());

  const auto& hist = families[0].metric(0).histogram();

  // Verify sample count
  EXPECT_EQ(percent_values.size(), hist.sample_count());

  // Verify sum is in 0-1 scale (should be ~5.25), not in millions.
  // This is the key verification - the sum must be scaled by 1/PercentScale.
  // Allow some tolerance for circllhist approximation.
  EXPECT_GT(hist.sample_sum(), expected_sum * 0.95);
  EXPECT_LT(hist.sample_sum(), expected_sum * 1.05);

  // Verify bucket configuration matches what we set
  ASSERT_EQ(percent_buckets.size(), hist.bucket_size());

  // Verify bucket bounds are in 0-1 scale (our configured percent buckets)
  for (size_t i = 0; i < percent_buckets.size(); ++i) {
    EXPECT_EQ(percent_buckets[i], hist.bucket(i).upper_bound())
        << "Bucket " << i << " upper bound should match configured bucket";
  }

  EXPECT_EQ(2, hist.bucket(0).cumulative_count()); // <= 0.25
  EXPECT_EQ(3, hist.bucket(1).cumulative_count()); // <= 0.50
  EXPECT_EQ(4, hist.bucket(2).cumulative_count()); // <= 0.75
  EXPECT_EQ(5, hist.bucket(3).cumulative_count()); // <= 1.00
  EXPECT_EQ(7, hist.bucket(4).cumulative_count()); // <= 1.50
  EXPECT_EQ(7, hist.bucket(5).cumulative_count()); // <= 2.00
}

// Test that cumulative counts are preserved correctly.
// This verifies the cumulativeCountLessThanOrEqualToValue queries produce accurate results.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramCumulativeAccuracy) {
  Stats::Histogram& h1 = makeHistogram("histogram_cumulative", Stats::Histogram::Unit::Unspecified);

  // Record values with known counts at specific ranges
  for (int i = 0; i < 10; ++i) {
    recordValue(h1, 5); // 10 samples at 5
  }
  for (int i = 0; i < 20; ++i) {
    recordValue(h1, 50); // 20 samples at 50
  }
  for (int i = 0; i < 30; ++i) {
    recordValue(h1, 500); // 30 samples at 500
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_cumulative");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  // Use a high max_buckets to avoid schema reduction, allowing us to test
  // exact bucket containment at the finest schema (5).
  params.native_histogram_max_buckets_ = 500;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  PrometheusStatsFormatter::statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                                      endpoints_helper_->cm_, response_headers,
                                                      response, params, custom_namespaces_);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();
  const auto schema = hist.schema();

  // With high max_buckets, we should get the finest schema (4)
  EXPECT_EQ(4, schema) << "Should use finest schema with high max_buckets";

  // Verify total counts
  EXPECT_EQ(60, hist.sample_count()); // 10 + 20 + 30
  EXPECT_EQ(0, hist.zero_count());

  // Decode buckets and verify counts
  auto buckets = NativeHistogramDecoder::decodePositiveBuckets(hist);

  // Count samples in buckets containing each recorded value.
  // At schema 4, bucket boundaries are precise enough to verify containment.
  uint64_t count_around_5 = 0;
  uint64_t count_around_50 = 0;
  uint64_t count_around_500 = 0;

  for (const auto& [idx, count] : buckets) {
    double lower = NativeHistogramDecoder::bucketLowerBound(schema, idx);
    double upper = NativeHistogramDecoder::bucketUpperBound(schema, idx);

    // Bucket i covers (base^i, base^(i+1)], i.e., exclusive lower, inclusive upper.
    if (lower < 5.0 && upper >= 5.0) {
      count_around_5 += count;
    }
    if (lower < 50.0 && upper >= 50.0) {
      count_around_50 += count;
    }
    if (lower < 500.0 && upper >= 500.0) {
      count_around_500 += count;
    }
  }

  // Verify the counts are approximately correct (circllhist may distribute across buckets)
  // We expect most samples to be in the bucket containing the value
  EXPECT_GE(count_around_5, 5) << "Most samples at 5 should be in bucket containing 5";
  EXPECT_GE(count_around_50, 10) << "Most samples at 50 should be in bucket containing 50";
  EXPECT_GE(count_around_500, 15) << "Most samples at 500 should be in bucket containing 500";

  // Total should be exact
  EXPECT_EQ(60, decodeTotalCountFromBuckets(hist));
}

// Test native histogram with a distribution resembling real-world data.
// Records hundreds of distinct values with a bell-curve-like distribution centered around 500.
// This validates that the native histogram accurately represents dense, realistic data.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramNormalDistribution) {
  Stats::Histogram& h1 = makeHistogram("histogram_normal", Stats::Histogram::Unit::Milliseconds);

  // Generate a normal distribution with hundreds of unique values.
  // Use values from 10 to 1000, centered at 500 with standard deviation of 150.
  // The count at each value follows a Gaussian curve.
  constexpr double mean = 500.0;
  constexpr double stddev = 150.0;
  constexpr double peak_count = 50.0; // Maximum count at the mean

  std::vector<std::pair<uint64_t, uint64_t>> value_counts;

  // Generate values from 10 to 1000 in increments of 2 (496 distinct values)
  for (uint64_t v = 10; v <= 1000; v += 2) {
    // Calculate Gaussian weight: ``exp(-0.5 * ((v - mean) / stddev)^2)``
    double z = (static_cast<double>(v) - mean) / stddev;
    double weight = std::exp(-0.5 * z * z);
    // Scale to get count, minimum of 1 to ensure all values are recorded
    uint64_t count = std::max(static_cast<uint64_t>(1), static_cast<uint64_t>(peak_count * weight));
    value_counts.emplace_back(v, count);
  }

  // Also add some long-tail values beyond 1000 to simulate realistic latency spikes
  for (uint64_t v = 1050; v <= 2000; v += 50) {
    value_counts.emplace_back(v, 1);
  }

  uint64_t total_count = 0;
  uint64_t weighted_sum = 0;
  for (const auto& [value, count] : value_counts) {
    for (uint64_t i = 0; i < count; ++i) {
      recordValue(h1, value);
    }
    total_count += count;
    weighted_sum += value * count;
  }
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_normal");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  EXPECT_EQ(total_count, hist.sample_count());

  // Verify sum is reasonable (should be close to weighted_sum, accounting for circllhist
  // approximation)
  EXPECT_GT(hist.sample_sum(), weighted_sum * 0.95);
  EXPECT_LT(hist.sample_sum(), weighted_sum * 1.05);

  EXPECT_EQ(0, hist.zero_count());

  EXPECT_GT(hist.positive_delta_size(), 0);
  EXPECT_GT(hist.positive_span_size(), 0);

  // Decode and verify bucket structure
  const DecodedNativeHistogram decoded(hist);

  EXPECT_EQ(hist.schema(), 1);

  // Total from decoded buckets should match sample count
  EXPECT_EQ(total_count, decoded.totalPositiveBucketCount());

  // Verify the distribution shape: buckets near the center (500) should have more samples
  // than buckets at the tails. Find the bucket containing 500 and verify it has high count.
  uint64_t count_near_center = 0;
  uint64_t count_at_tails = 0;
  for (const auto& bucket : decoded.positive_buckets) {
    // Buckets with bounds overlapping [350, 650] are "near center"
    if (bucket.upper_bound >= 350 && bucket.lower_bound <= 650) {
      count_near_center += bucket.count;
    }
    // Buckets with upper_bound < 100 or lower_bound > 1000 are "tails"
    if (bucket.upper_bound < 100 || bucket.lower_bound > 1000) {
      count_at_tails += bucket.count;
    }
  }

  // The center should have significantly more samples than the tails
  EXPECT_GT(count_near_center, count_at_tails * 2)
      << "Center of distribution should have more samples than tails";

  // Verify bucket indices are all positive (all values > zero_threshold)
  for (const auto& bucket : decoded.positive_buckets) {
    EXPECT_GT(bucket.count, 0);
  }

  // Verify we can reconstruct approximate percentiles from the native histogram.
  // The median (~50th percentile) should be near 500 (the mean).
  uint64_t cumulative = 0;
  double median_bucket_upper = 0;
  for (const auto& bucket : decoded.positive_buckets) {
    cumulative += bucket.count;
    if (cumulative >= total_count / 2) {
      median_bucket_upper = bucket.upper_bound;
      break;
    }
  }
  // Median should be in a bucket whose upper bound is between 400 and 600
  EXPECT_GE(median_bucket_upper, 400) << "Median bucket should be near center of distribution";
  EXPECT_LE(median_bucket_upper, 600) << "Median bucket should be near center of distribution";

  // Log some info about the distribution for debugging
  ENVOY_LOG_MISC(info,
                 "Normal distribution test: {} distinct values, {} total samples, "
                 "{} positive buckets, schema {}",
                 value_counts.size(), total_count, decoded.positive_buckets.size(), hist.schema());
}

// Test that triggers the schema selection fallback path when even the coarsest schema
// exceeds max_buckets.
TEST_F(RealHistogramNativePrometheusTest, NativeHistogramSchemaFallback) {
  Stats::Histogram& h1 = makeHistogram("histogram_fallback", Stats::Histogram::Unit::Unspecified);

  // Record two values that span more than 65536x (the bucket width at schema -4).
  recordValue(h1, 1);
  recordValue(h1, 1000000000);
  mergeHistograms();

  auto parent_histogram = getParentHistogram("histogram_fallback");
  ASSERT_NE(nullptr, parent_histogram);

  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;
  std::vector<Stats::ParentHistogramSharedPtr> histograms = {parent_histogram};
  std::vector<Stats::TextReadoutSharedPtr> text_readouts;

  StatsParams params;
  params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::PrometheusNative;
  // Set max_buckets to 1 so that even schema -4 will exceed the limit during the loop,
  // triggering the fallback path.
  params.native_histogram_max_buckets_ = 1;

  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  const uint64_t size = PrometheusStatsFormatter::statsAsPrometheusProtobuf(
      counters, gauges, histograms, text_readouts, endpoints_helper_->cm_, response_headers,
      response, params, custom_namespaces_);
  EXPECT_EQ(1UL, size);

  auto families = parsePrometheusProtobuf(response.toString());
  ASSERT_EQ(1, families.size());

  const auto& hist = families[0].metric(0).histogram();

  // The fallback should use schema -4 (the coarsest schema).
  EXPECT_EQ(-4, hist.schema()) << "Fallback should use coarsest schema -4";

  EXPECT_EQ(2, hist.sample_count());
  EXPECT_EQ(0, hist.zero_count()); // No zeros recorded

  // Decode buckets and verify we got valid output despite exceeding max_buckets
  const DecodedNativeHistogram decoded(hist);

  ASSERT_EQ(2, decoded.positive_buckets.size())
      << "Should have 2 buckets at schema -4 for values spanning > 65536x";

  EXPECT_EQ(1, decoded.positive_buckets[0].count);
  EXPECT_EQ(1, decoded.positive_buckets[1].count);
  EXPECT_EQ(2, decoded.totalPositiveBucketCount());

  // At schema -4, base = 2^16 = 65536
  // Value 1: bucket index = ceil(log(1)/log(65536)) - 1 = ceil(0) - 1 = -1
  // Value 1000000000: bucket index = ceil(log(1e9)/log(65536)) - 1 = ceil(1.87) - 1 = 1
  // So indices should be -1 and 1.
  EXPECT_EQ(-1, decoded.positive_buckets[0].index) << "Value 1 should be in bucket -1 at schema -4";
  EXPECT_EQ(1, decoded.positive_buckets[1].index)
      << "Value 1000000000 should be in bucket 1 at schema -4";
}

} // namespace Server
} // namespace Envoy
