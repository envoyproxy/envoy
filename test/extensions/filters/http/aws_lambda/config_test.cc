#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "extensions/filters/http/aws_lambda/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
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
  EXPECT_NE(route_specific_config_ptr, nullptr);
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
