#include <string>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

class SetMetadataIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  SetMetadataIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetMetadataIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that the filter loads correctly and processes a basic request
TEST_P(SetMetadataIntegrationTest, BasicRequestWithMetadata) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  apply_on: REQUEST
  metadata:
  - metadata_namespace: envoy.test.request
    format_string:
      text_format_source:
        inline_string: "%REQ(:method)%"
    type: STRING
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test response-only metadata processing
TEST_P(SetMetadataIntegrationTest, ResponseOnlyMetadata) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  apply_on: RESPONSE
  metadata:
  - metadata_namespace: envoy.test.response
    format_string:
      text_format_source:
        inline_string: "%RESPONSE_CODE%"
    type: NUMBER
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/api"},
                                     {":scheme", "http"},
                                     {":authority", "api.example.com"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test both request and response metadata processing
TEST_P(SetMetadataIntegrationTest, BothRequestAndResponseMetadata) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  apply_on: BOTH
  metadata:
  - metadata_namespace: envoy.test.comprehensive
    format_string:
      text_format_source:
        inline_string: "%REQ(:method)%-%RESPONSE_CODE%"
    type: STRING
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                                                   {":path", "/update"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "example.com"}},
                                    0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test Base64 decoding
TEST_P(SetMetadataIntegrationTest, Base64EncodedMetadata) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.test.encoded
    format_string:
      text_format_source:
        inline_string: "dGVzdF92YWx1ZQ=="  # "test_value" in base64
    type: STRING
    encode: BASE64
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/encoded"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "test.com"}},
                                    0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test multiple metadata types
TEST_P(SetMetadataIntegrationTest, MultipleMetadataTypes) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  # String metadata
  - metadata_namespace: envoy.test.string
    format_string:
      text_format_source:
        inline_string: "%REQ(:authority)%"
    type: STRING
  # Number metadata
  - metadata_namespace: envoy.test.number
    format_string:
      text_format_source:
        inline_string: "42"
    type: NUMBER
  # Static metadata
  - metadata_namespace: envoy.test.static
    value:
      service_name: "test-service"
      version: "1.0.0"
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/multi"},
                                     {":scheme", "http"},
                                     {":authority", "multi.example.com"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test backward compatibility with deprecated API
TEST_P(SetMetadataIntegrationTest, BackwardCompatibilityDeprecatedApi) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  # Using deprecated API
  metadata_namespace: legacy.namespace
  value:
    legacy_field: "legacy_value"
    service_id: "legacy-service"
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/legacy"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "legacy.com"}},
                                    0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test default apply_on behavior (should default to REQUEST)
TEST_P(SetMetadataIntegrationTest, DefaultApplyOnRequestOnly) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  # No apply_on specified
  metadata:
  - metadata_namespace: envoy.test.default
    format_string:
      text_format_source:
        inline_string: "default_behavior"
)EOF";

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/default"}, {":scheme", "http"}, {":authority", "host"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test filter configuration with access log format (without verifying the actual log)
TEST_P(SetMetadataIntegrationTest, ConfigurationWithAccessLogFormat) {
  const std::string filter_config = R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  apply_on: BOTH
  metadata:
  - metadata_namespace: als.request
    format_string:
      text_format_source:
        inline_string: "%REQ(x-trace-id?:not-set)%"
    type: STRING
  - metadata_namespace: als.response
    format_string:
      text_format_source:
        inline_string: "%RESPONSE_CODE%"
    type: NUMBER
  - metadata_namespace: als.service
    value:
      service_name: "user-service"
      environment: "production"
      version: "v2.1.0"
)EOF";

  // Configure access log format that would use the metadata
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* access_log = hcm.add_access_log();
        access_log->set_name("accesslog");

        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        access_log_config.set_path(TestEnvironment::temporaryPath("als_test.log"));

        // Configure format that would use dynamic metadata
        access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "trace_id=%DYNAMIC_METADATA(als.request:value)% "
            "response_code=%DYNAMIC_METADATA(als.response:value)% "
            "service=%DYNAMIC_METADATA(als.service:service_name)%");

        access_log->mutable_typed_config()->PackFrom(access_log_config);
      });

  initializeFilter(filter_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/api/users"},
                                     {":scheme", "http"},
                                     {":authority", "api.company.com"},
                                     {"x-trace-id", "abc123def456"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
