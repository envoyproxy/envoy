#include "envoy/config/metrics/v2/stats.pb.h"

#include "common/config/well_known_names.h"
#include "common/stats/tag_producer_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(TagProducerTest, CheckConstructor) {
  envoy::config::metrics::v2::StatsConfig stats_config;

  // Should pass there were no tag name conflict.
  auto& tag_specifier1 = *stats_config.mutable_stats_tags()->Add();
  tag_specifier1.set_tag_name("test.x");
  tag_specifier1.set_fixed_value("xxx");
  TagProducerImpl{stats_config};

  // Should raise an error when duplicate tag names are specified.
  auto& tag_specifier2 = *stats_config.mutable_stats_tags()->Add();
  tag_specifier2.set_tag_name("test.x");
  tag_specifier2.set_fixed_value("yyy");
  EXPECT_THROW_WITH_MESSAGE(TagProducerImpl{stats_config}, EnvoyException,
                            fmt::format("Tag name '{}' specified twice.", "test.x"));

  // Also should raise an error when user defined tag name conflicts with Envoy's default tag names.
  stats_config.clear_stats_tags();
  stats_config.mutable_use_all_default_tags()->set_value(true);
  auto& custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name(Config::TagNames::get().CLUSTER_NAME);
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      fmt::format("Tag name '{}' specified twice.", Config::TagNames::get().CLUSTER_NAME));

  // Non-default custom name without regex should throw
  stats_config.mutable_use_all_default_tags()->set_value(true);
  stats_config.clear_stats_tags();
  custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");

  // Also empty regex should throw
  stats_config.mutable_use_all_default_tags()->set_value(true);
  stats_config.clear_stats_tags();
  custom_tag_extractor = *stats_config.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  custom_tag_extractor.set_regex("");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl{stats_config}, EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");
}

} // namespace Stats
} // namespace Envoy
