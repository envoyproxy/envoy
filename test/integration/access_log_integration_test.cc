#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {

class AccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test COALESCE formatter returning the first available value.
TEST_P(AccessLogIntegrationTest, CoalesceFormatterFirstValueAvailable) {
  // The default :authority header is "sni.lyft.com".
  useAccessLog(
      R"(host=%COALESCE({"operators": [{"command": "REQ", "param": ":authority"}, {"command": "REQ", "param": "host"}]})%)");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  std::string log = waitForAccessLog(access_log_name_);
  // The :authority header (sni.lyft.com) should be logged since it's the first available value.
  EXPECT_THAT(log, HasSubstr("host=sni.lyft.com"));
}

// Test COALESCE formatter with fallback when first operator returns null.
TEST_P(AccessLogIntegrationTest, CoalesceFormatterFallback) {
  // Use a header that won't be present as first operator, fallback to :authority.
  useAccessLog(
      R"(host=%COALESCE({"operators": [{"command": "REQ", "param": "x-custom-host"}, {"command": "REQ", "param": ":authority"}]})%)");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  std::string log = waitForAccessLog(access_log_name_);
  // x-custom-host is not present, so should fallback to :authority (sni.lyft.com).
  EXPECT_THAT(log, HasSubstr("host=sni.lyft.com"));
}

// Test COALESCE formatter combined with other formatters in the same log line.
TEST_P(AccessLogIntegrationTest, CoalesceFormatterWithOtherFormatters) {
  useAccessLog(
      R"(method=%REQ(:METHOD)% host=%COALESCE({"operators": [{"command": "REQ", "param": ":authority"}]})% protocol=%PROTOCOL%)");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("method=GET"));
  EXPECT_THAT(log, HasSubstr("host=sni.lyft.com"));
  EXPECT_THAT(log, HasSubstr("protocol=HTTP/1.1"));
}

// Test COALESCE formatter with max_length truncation.
TEST_P(AccessLogIntegrationTest, CoalesceFormatterMaxLength) {
  // The default :authority header is "sni.lyft.com", truncated to 3 chars should be "sni".
  useAccessLog(R"(host=%COALESCE({"operators": [{"command": "REQ", "param": ":authority"}]}):3%)");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  std::string log = waitForAccessLog(access_log_name_);
  // :authority is "sni.lyft.com", truncated to 3 chars should be "sni".
  EXPECT_THAT(log, HasSubstr("host=sni"));
}

TEST_P(AccessLogIntegrationTest, DownstreamDisconnectBeforeHeadersResponseCode) {
  useAccessLog("RESPONSE_CODE=%RESPONSE_CODE%;CEL_METHOD=%CEL(request.headers[':method'])%");
  testRouterDownstreamDisconnectBeforeRequestComplete();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("RESPONSE_CODE=0;CEL_METHOD=GET"));
}

TEST_P(AccessLogIntegrationTest, ShouldReplaceInvalidUtf8) {
  // Add incomplete UTF-8 strings.
  default_request_headers_.setForwardedFor("\xec");

  // Update access logs to use json format sorted.
  access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier(
      [this](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        auto* access_log_config_to_clobber = hcm.add_access_log();
        access_log_config.set_path(access_log_name_);

        auto* log_format = access_log_config.mutable_log_format();
        auto* json = log_format->mutable_json_format();
        Envoy::Protobuf::Value v;
        v.set_string_value("%REQ(X-FORWARDED-FOR)%");
        auto fields = json->mutable_fields();
        (*fields)["x_forwarded_for"] = v;
        access_log_config_to_clobber->mutable_typed_config()->PackFrom(access_log_config);
      });
  testRouterDownstreamDisconnectBeforeRequestComplete();
  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("x_forwarded_for\":\"\\u00ec"));
}
} // namespace Envoy
