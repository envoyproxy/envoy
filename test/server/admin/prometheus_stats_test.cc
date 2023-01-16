#include <regex>
#include <string>
#include <vector>

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/server/admin/prometheus_stats.h"

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

  void addTextReadout(const std::string& name, const std::string& value,
                      Stats::StatNameTagVector cluster_tags) {
    Stats::StatNameManagedStorage name_storage(baseName(name, cluster_tags), *symbol_table_);
    Stats::StatNameManagedStorage tag_extracted_name_storage(name, *symbol_table_);
    Stats::TextReadoutSharedPtr textReadout = alloc_.makeTextReadout(
        name_storage.statName(), tag_extracted_name_storage.statName(), cluster_tags);
    textReadout->set(value);
    textReadouts_.push_back(textReadout);
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
    textReadouts_.clear();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::AllocatorImpl alloc_;
  Stats::StatNamePool pool_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
  std::vector<Stats::TextReadoutSharedPtr> textReadouts_;
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

} // namespace Server
} // namespace Envoy
