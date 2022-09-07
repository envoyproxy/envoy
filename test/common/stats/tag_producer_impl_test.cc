#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/tag_producer_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class TagProducerTest : public testing::Test {
protected:
  void addSpecifier(const char* name, const char* value) {
    auto& specifier = *stats_config_.mutable_stats_tags()->Add();
    specifier.set_tag_name(name);
    specifier.set_regex(value);
  }

  void checkTags(const TagVector& expected, const TagVector& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (uint32_t i = 0; i < actual.size(); ++i) {
      EXPECT_EQ(expected[i].name_, actual[i].name_) << " index=" << i;
      EXPECT_EQ(expected[i].value_, actual[i].value_) << " index=" << i;
    }
  }

  envoy::config::metrics::v3::StatsConfig stats_config_;
};

TEST_F(TagProducerTest, CheckConstructor) {
  // Should pass there were no tag name conflict.
  addSpecifier("test.x", "xxx");
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {}));
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {{"test.y", "yyy"}}));

  // Should not raise an error when duplicate tag names between cli and config.
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {{"test.x", "yyy"}}));

  // Should not raise an error when duplicate tag names are specified.
  addSpecifier("test.x", "yyy");
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {{"test.y", "yyy"}}));

  // Should not raise an error when a cli tag names conflicts with Envoy's default tag names.
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {{Config::TagNames::get().CLUSTER_NAME, "yyy"}}));

  // Also should raise an error when user defined tag name conflicts with Envoy's default tag names.
  stats_config_.clear_stats_tags();
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  auto& custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name(Config::TagNames::get().CLUSTER_NAME);
  EXPECT_NO_THROW(TagProducerImpl(stats_config_, {}));

  // Non-default custom name without regex should throw
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  stats_config_.clear_stats_tags();
  custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl(stats_config_, {}), EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");

  // Also empty regex should throw
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  stats_config_.clear_stats_tags();
  custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  custom_tag_extractor.set_regex("");
  EXPECT_THROW_WITH_MESSAGE(
      TagProducerImpl(stats_config_, {}), EnvoyException,
      "No regex specified for tag specifier and no default regex for name: 'test_extractor'");
}

TEST_F(TagProducerTest, DuplicateConfigTagBehavior) {
  addSpecifier("envoy.response_code", "\\.(response_code=(\\d{3}));");
  addSpecifier("envoy.response_code", "_rq(_(\\d{3}))$");
  {
    TagProducerImpl producer{stats_config_};
    TagVector tags;
    EXPECT_EQ("cluster.;upstream_rq",
              producer.produceTags("cluster.xds-grpc.response_code=300;upstream_rq_200", tags));
    checkTags(
        TagVector{
            {"envoy.response_code", "200"},
            {"envoy.response_code", "300"},
            {"envoy.response_code", "200"},
            {"envoy.cluster_name", "xds-grpc"},
        },
        tags);
  }
}

TEST_F(TagProducerTest, DuplicateConfigCliTagBehavior) {
  addSpecifier("response", "\\.(response_code=(\\d{3}));");
  {
    TagProducerImpl producer{stats_config_, {{"response", "fixed"}}};
    TagVector tags;
    EXPECT_EQ("cluster.;upstream_rq",
              producer.produceTags("cluster.xds-grpc.response_code=300;upstream_rq_200", tags));
    checkTags(
        TagVector{
            {"response", "fixed"},
            {"envoy.response_code", "200"},
            {"response", "300"},
            {"envoy.cluster_name", "xds-grpc"},
        },
        tags);
  }
}

} // namespace Stats
} // namespace Envoy
