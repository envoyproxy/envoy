#include <regex>
#include <string>
#include <vector>

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/server/admin/prometheus_stats_formatter.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

class PrometheusStatsFormatterTest : public testing::Test {};

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
