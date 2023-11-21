#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"

#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"
#include "source/extensions/filters/http/aws_request_signing/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

TEST(AwsRequestSigningFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
service_name: s3
region: us-west-2
host_rewrite: new-host
match_excluded_headers:
  - prefix: x-envoy
  - exact: foo
  - exact: bar
  )EOF";

  AwsRequestSigningProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  AwsRequestSigningProtoConfig expected_config;
  expected_config.set_service_name("s3");
  expected_config.set_region("us-west-2");
  expected_config.set_host_rewrite("new-host");
  expected_config.add_match_excluded_headers()->set_prefix("x-envoy");
  expected_config.add_match_excluded_headers()->set_exact("foo");
  expected_config.add_match_excluded_headers()->set_exact("bar");

  Protobuf::util::MessageDifferencer differencer;
  differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUAL);
  differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);
  EXPECT_TRUE(differencer.Compare(expected_config, proto_config));

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsRequestSigningFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(AwsRequestSigningFilterConfigTest, RouteSpecificFilterConfig) {
  const std::string yaml = R"EOF(
aws_request_signing:
  service_name: s3
  region: us-west-2
  host_rewrite: new-host
  match_excluded_headers:
    - prefix: x-envoy
    - exact: foo
    - exact: bar
stat_prefix: foo_prefix
  )EOF";

  AwsRequestSigningProtoPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsRequestSigningFilterFactory factory;

  const auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getNullValidationVisitor());
  ASSERT_NE(route_config, nullptr);
}

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
