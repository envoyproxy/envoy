#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"
#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using EnableMode = envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit_EnableMode;

TEST(Factory, GlobalEmptyConfig) {
  const std::string yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.server_factory_context_.dispatcher_, createTimer_(_)).Times(0);
  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST(Factory, RouteSpecificFilterConfig) {
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

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->fillInterval().count(), 100);
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_REQUEST_AND_RESPONSE);
  EXPECT_FALSE(config->tokenBucket() == nullptr);
  EXPECT_EQ(config->enableResponseTrailers(), true);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("test-bandwidth-response-delay-ms"));
}

TEST(Factory, RouteSpecificFilterConfigDisabledByDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 10
  fill_interval: 0.1s
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->enableMode(), EnableMode::BandwidthLimit_EnableMode_DISABLED);
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->fillInterval().count(), 100);
}

TEST(Factory, RouteSpecificFilterConfigDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 10
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->fillInterval().count(), 50);
  // default trailers
  EXPECT_EQ(config->enableResponseTrailers(), false);
  EXPECT_EQ(const_cast<FilterConfig*>(config)->requestDelayTrailer(),
            Http::LowerCaseString("bandwidth-request-delay-ms"));
  EXPECT_EQ(const_cast<FilterConfig*>(config)->responseDelayTrailer(),
            Http::LowerCaseString("bandwidth-response-delay-ms"));
}

TEST(Factory, PerRouteConfigNoLimits) {
  const std::string config_yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}
} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
