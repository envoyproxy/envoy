#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/tag_producer_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class TagProducerTest : public testing::Test {
protected:
  void addSpecifier(const std::string& name, const std::string& value) {
    auto& specifier = *stats_config_.mutable_stats_tags()->Add();
    specifier.set_tag_name(name);
    specifier.set_regex(value);
  }

  void checkTags(const TagVector& expected, const TagVector& actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (uint32_t i = 0; i < actual.size(); ++i) {
      EXPECT_EQ(expected[i].name_, actual[i].name_) << " index=" << i;
      if (expected[i].value_ != "*") {
        EXPECT_EQ(expected[i].value_, actual[i].value_) << " index=" << i;
      }
    }
  }

  envoy::config::metrics::v3::StatsConfig stats_config_;
  const Config::TagNameValues tag_name_values_;
};

TEST_F(TagProducerTest, CheckConstructor) {
  // Should pass there were no tag name conflict.
  addSpecifier("test.x", "xxx");
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_, {}).status().ok());
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_, {{"test.y", "yyy"}}).status().ok());

  // Should not raise an error when duplicate tag names between cli and config.
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_, {{"test.x", "yyy"}}).status().ok());

  // Should not raise an error when duplicate tag names are specified.
  addSpecifier("test.x", "yyy");
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_, {{"test.y", "yyy"}}).status().ok());

  // Should not raise an error when a cli tag names conflicts with Envoy's default tag names.
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_,
                                                 {{Config::TagNames::get().CLUSTER_NAME, "yyy"}})
                  .status()
                  .ok());

  // Also should raise an error when user defined tag name conflicts with Envoy's default tag names.
  stats_config_.clear_stats_tags();
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  auto& custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name(Config::TagNames::get().CLUSTER_NAME);
  EXPECT_TRUE(TagProducerImpl::createTagProducer(stats_config_, {}).status().ok());

  // Non-default custom name without regex should throw
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  stats_config_.clear_stats_tags();
  custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  EXPECT_EQ(TagProducerImpl::createTagProducer(stats_config_, {}).status().message(),
            "No regex specified for tag specifier and no default regex for name: 'test_extractor'");

  // Also empty regex should throw
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  stats_config_.clear_stats_tags();
  custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("test_extractor");
  custom_tag_extractor.set_regex("");
  EXPECT_EQ(TagProducerImpl::createTagProducer(stats_config_, {}).status().message(),
            "No regex specified for tag specifier and no default regex for name: 'test_extractor'");

  // Also invalid regex should throw
  stats_config_.mutable_use_all_default_tags()->set_value(true);
  stats_config_.clear_stats_tags();
  custom_tag_extractor = *stats_config_.mutable_stats_tags()->Add();
  custom_tag_extractor.set_tag_name("");
  custom_tag_extractor.set_regex("...");
  EXPECT_EQ(TagProducerImpl::createTagProducer(stats_config_, {}).status().message(),
            "tag_name cannot be empty");
}

TEST_F(TagProducerTest, DuplicateConfigTagBehavior) {
  addSpecifier(tag_name_values_.RESPONSE_CODE, "\\.(response_code=(\\d{3}));");
  {
    auto producer = TagProducerImpl::createTagProducer(stats_config_, {}).value();
    TagVector tags;
    std::string extracted_name;
    EXPECT_LOG_CONTAINS("warn", "Skipping duplicate tag",
                        extracted_name = producer->produceTags(
                            "cluster.xds-grpc.response_code=300;upstream_rq_200", tags));
    EXPECT_TRUE(extracted_name == "cluster.;upstream_rq_200" ||
                extracted_name == "cluster.response_code=300;upstream_rq")
        << "extracted_name=" << extracted_name;
    checkTags(
        TagVector{
            {tag_name_values_.RESPONSE_CODE, "*"},
            {tag_name_values_.CLUSTER_NAME, "xds-grpc"},
        },
        tags);
  }
}

TEST_F(TagProducerTest, DuplicateConfigCliTagBehavior) {
  addSpecifier(tag_name_values_.RESPONSE_CODE, "\\.(response_code=(\\d{3}));");
  {
    auto producer = TagProducerImpl::createTagProducer(stats_config_,
                                                       {{tag_name_values_.RESPONSE_CODE, "fixed"}})
                        .value();
    TagVector tags;
    const std::string stat_name = "cluster.xds-grpc.response_code=300;upstream_rq_200";
    std::string extracted_name;
    EXPECT_LOG_CONTAINS("warn", "Skipping duplicate tag",
                        extracted_name = producer->produceTags(stat_name, tags));
    EXPECT_TRUE(extracted_name == "cluster.;upstream_rq_200" ||
                extracted_name == "cluster.response_code=300;upstream_rq" ||
                extracted_name == stat_name)
        << "extracted_name=" << extracted_name;
    checkTags(
        TagVector{
            {tag_name_values_.RESPONSE_CODE, "*"},
            {tag_name_values_.CLUSTER_NAME, "xds-grpc"},
        },
        tags);
  }
}

TEST_F(TagProducerTest, Fixed) {
  const TagVector tag_config{{"my-tag", "fixed"}};
  auto producer(TagProducerImpl::createTagProducer(stats_config_, tag_config).value());
  TagVector tags;
  EXPECT_EQ("stat-name", producer->produceTags("stat-name", tags));
  checkTags(tag_config, tags);
}

// Test that fixed tags both from cli and from stats_config are returned from `fixedTags()`.
TEST_F(TagProducerTest, FixedTags) {
  const TagVector tag_config{{"my-tag", "fixed"}};

  auto& specifier = *stats_config_.mutable_stats_tags()->Add();
  specifier.set_tag_name("tag2");
  specifier.set_fixed_value("value2");

  // This one isn't a fixed value so it won't be included.
  addSpecifier("regex", "value");

  auto producer = TagProducerImpl::createTagProducer(stats_config_, tag_config).value();
  const auto& tags = producer->fixedTags();
  EXPECT_THAT(tags, testing::UnorderedElementsAreArray(TagVector{
                        {"my-tag", "fixed"},
                        {"tag2", "value2"},
                    }));
}

TEST(UtilityTest, createTagProducer) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto producer = TagProducerImpl::createTagProducer(bootstrap.stats_config(), {}).value();
  ASSERT_TRUE(producer != nullptr);
  Stats::TagVector tags;
  auto extracted_name = producer->produceTags("http.config_test.rq_total", tags);
  ASSERT_EQ(extracted_name, "http.rq_total");
  ASSERT_EQ(tags.size(), 1);
}

TEST(UtilityTest, createTagProducerWithDefaultTgs) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto producer =
      TagProducerImpl::createTagProducer(bootstrap.stats_config(), {{"foo", "bar"}}).value();
  ASSERT_TRUE(producer != nullptr);
  Stats::TagVector tags;
  auto extracted_name = producer->produceTags("http.config_test.rq_total", tags);
  EXPECT_EQ(extracted_name, "http.rq_total");
  EXPECT_EQ(tags.size(), 2);
}

} // namespace Stats
} // namespace Envoy
