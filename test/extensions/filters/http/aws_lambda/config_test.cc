#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"
#include "source/extensions/filters/http/aws_lambda/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Truly;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {
namespace {

using LambdaConfig = envoy::extensions::filters::http::aws_lambda::v3::Config;
using LambdaPerRouteConfig = envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig;

TEST(AwsLambdaFilterConfigTest, ValidConfigCreatesFilter) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto settings = filter->settingsForTest();
    return settings.payloadPassthrough() &&
           settings.invocationMode() == InvocationMode::Asynchronous;
  };

  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

/**
 * The default for passthrough is false.
 * The default for invocation_mode is Synchronous.
 */
TEST(AwsLambdaFilterConfigTest, ValidConfigVerifyDefaults) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto settings = filter->settingsForTest();
    return settings.payloadPassthrough() == false &&
           settings.invocationMode() == InvocationMode::Synchronous;
  };

  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

TEST(AwsLambdaFilterConfigTest, ValidPerRouteConfigCreatesFilter) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:function:fun"
    payload_passthrough: true
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto route_specific_config_ptr = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getStrictValidationVisitor());
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  ASSERT_NE(route_specific_config_ptr, nullptr);
  auto filter_settings_ptr =
      std::static_pointer_cast<const FilterSettings>(route_specific_config_ptr);
  EXPECT_TRUE(filter_settings_ptr->payloadPassthrough());
  EXPECT_EQ(InvocationMode::Synchronous, filter_settings_ptr->invocationMode());
}

TEST(AwsLambdaFilterConfigTest, InvalidARNThrows) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:fun"
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  EXPECT_THROW(
      factory.createFilterFactoryFromProto(proto_config, "stats", context).status().IgnoreError(),
      EnvoyException);
}

TEST(AwsLambdaFilterConfigTest, PerRouteConfigWithInvalidARNThrows) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:fun"
    payload_passthrough: true
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  EXPECT_THROW(factory.createRouteSpecificFilterConfig(
                   proto_config, context, ProtobufMessage::getStrictValidationVisitor()),
               EnvoyException);
}

TEST(AwsLambdaFilterConfigTest, AsynchrnousPerRouteConfig) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:function:fun"
    payload_passthrough: false
    invocation_mode: asynchronous
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto route_specific_config_ptr = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getStrictValidationVisitor());
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  ASSERT_NE(route_specific_config_ptr, nullptr);
  auto filter_settings_ptr =
      std::static_pointer_cast<const FilterSettings>(route_specific_config_ptr);
  EXPECT_FALSE(filter_settings_ptr->payloadPassthrough());
  EXPECT_EQ(InvocationMode::Asynchronous, filter_settings_ptr->invocationMode());
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
