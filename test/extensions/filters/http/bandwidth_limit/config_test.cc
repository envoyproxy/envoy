#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"
#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using EnableMode = envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit_EnableMode;

class FactoryTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

TEST_F(FactoryTest, GlobalEmptyConfig) {
  const std::string yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  auto callback =
      factory.createFilterFactoryFromProto(*proto_config, "stats", factory_context_).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST_F(FactoryTest, RouteSpecificFilterConfig) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 10
  fill_interval: 0.1s
  enable_response_trailers: true
  response_trailer_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 100);
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_REQUEST_AND_RESPONSE);
  EXPECT_FALSE(config->bucketAndStats(mock_stream_info_)->bucket() == nullptr);
  EXPECT_EQ(config->enableResponseTrailers(), true);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-response-delay-ms"));
}

TEST_F(FactoryTest, RouteSpecificFilterConfigDisabledByDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 10
  fill_interval: 0.1s
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_DISABLED);
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 100);
}

TEST_F(FactoryTest, RouteSpecificFilterConfigDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 10
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  EXPECT_CALL(factory_context_.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context_.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->bucketAndStats(mock_stream_info_)->fillInterval().count(), 50);
  // default trailers
  EXPECT_EQ(config->enableResponseTrailers(), false);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("bandwidth-response-delay-ms"));
}

TEST_F(FactoryTest, PerRouteConfigNoLimits) {
  const std::string config_yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  const auto result = factory.createRouteSpecificFilterConfig(
      *proto_config, factory_context_.server_factory_context_,
      ProtobufMessage::getNullValidationVisitor());
  EXPECT_EQ(result.status().message(), "limit must be set for per route filter config");
}

TEST_F(FactoryTest, FixedNameBucketSelectorUsesNamedBucket) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 50
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
  named_bucket_configurations:
  - name: test_explicit_bucket
    limit_kbps: 100
    fill_interval: 0.02s
  )";
  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);
  const auto result = factory.createRouteSpecificFilterConfig(
      *proto_config, factory_context_.server_factory_context_,
      ProtobufMessage::getNullValidationVisitor());
  ASSERT_OK(result);
  const FilterConfig& config = dynamic_cast<const FilterConfig&>(*result.value());
  EXPECT_THAT(config.bucketAndStats(mock_stream_info_)->fillInterval().count(), 20);
}

TEST_F(FactoryTest, FixedNameBucketSelectorCreatesDefaultBucket) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 50
  fill_interval: 0.03s
  named_bucket_selector:
    explicit_bucket: test_explicit_bucket
    create_bucket_if_not_existing: true
  )";
  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);
  const auto result = factory.createRouteSpecificFilterConfig(
      *proto_config, factory_context_.server_factory_context_,
      ProtobufMessage::getNullValidationVisitor());
  ASSERT_OK(result);
  const FilterConfig& config = dynamic_cast<const FilterConfig&>(*result.value());
  EXPECT_THAT(config.bucketAndStats(mock_stream_info_)->fillInterval().count(), 30);
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
