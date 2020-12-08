#include "extensions/filters/http/bandwidth_limit/config.h"
#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

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
enforce_threshold_kbps = 1024
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
limit_kbps = 10
fill_rate = 16
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
  EXPECT_EQ(config->enforce_threshold(), absl::nullopt);
  EXPECT_EQ(config->enable_mode(), EnableMode::BandwidthLimit_EnableMode_IngressAndEgress);
  EXPECT_FALSE(config->tokenBucket() == nullptr);
}

TEST(Factory, RouteSpecificEnforcedThresholdIgnored) {
  const std::string config_yaml = R"(
stat_prefix: test
enable_mode: IngressAndEgress
limit_kbps = 10
fill_rate = 16
enforce_threshold_kbps = 100
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(1);
  const auto route_config = factory.createRouteSpecificFilterConfig(
      *proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_EQ(config->enforce_threshold(), absl::nullopt);
  EXPECT_EQ(config->enable_mode(), EnableMode::BandwidthLimit_EnableMode_IngressAndEgress);
}

TEST(Factory, PerRouteConfigNoTokenBucket) {
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

TEST(Factory, FillTimerTooLow) {
  const std::string config_yaml = R"(
stat_prefix: test
enable_mode: IngressAndEgress
limit_kbps = 10
fill_rate = 16
enforce_threshold_kbps = 100
  )";

  BandwidthLimitFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(config_yaml, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_CALL(context.dispatcher_, createTimer_(_)).Times(1);
  EXPECT_THROW(factory.createRouteSpecificFilterConfig(*proto_config, context,
                                                       ProtobufMessage::getNullValidationVisitor()),
               EnvoyException);
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
