#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"
#include "extensions/filters/http/bandwidth_limit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using EnableMode =
    envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit_EnableMode;

TEST(Factory, GlobalEmptyConfig) {
  const std::string yaml = R"(
  stat_prefix: test
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST(Factory, RouteSpecificFilterConfig) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: IngressAndEgress
  limit_kbps: 10
  fill_rate: 32
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
  EXPECT_EQ(config->fill_rate(), 32);
  EXPECT_EQ(config->enable_mode(), EnableMode::BandwidthLimit_EnableMode_IngressAndEgress);
  EXPECT_FALSE(config->tokenBucket() == nullptr);
}

TEST(Factory, RouteSpecificFilterConfigDisabledByDefault) {
  const std::string config_yaml = R"(
  stat_prefix: test
  limit_kbps: 10
  fill_rate: 32
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->enable_mode(), EnableMode::BandwidthLimit_EnableMode_Disabled);
  EXPECT_EQ(config->limit(), 10);
  EXPECT_EQ(config->fill_rate(), 32);
}

TEST(Factory, RouteSpecificFilterConfigDefaultFillRate) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: IngressAndEgress
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
  EXPECT_EQ(config->fill_rate(), 16);
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

TEST(Factory, FillRateTooHigh) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: IngressAndEgress
  limit_kbps: 10
  fill_rate: 33
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
