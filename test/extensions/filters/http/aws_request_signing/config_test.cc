#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.validate.h"

#include "extensions/filters/http/aws_request_signing/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

using AwsRequestSigningProtoConfig =
    envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigning;

TEST(AwsRequestSigningFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
service_name: s3
region: us-west-2
  )EOF";

  AwsRequestSigningProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsRequestSigningFilterFactory factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
