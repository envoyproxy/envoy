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

TEST_P(AccessLogIntegrationTest, DownstreamDisconnectBeforeHeadersResponseCode) {
  useAccessLog("RESPONSE_CODE=%RESPONSE_CODE%");
  testRouterDownstreamDisconnectBeforeRequestComplete();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("RESPONSE_CODE=0"));
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
        Envoy::ProtobufWkt::Value v;
        v.set_string_value("%REQ(X-FORWARDED-FOR)%");
        auto fields = json->mutable_fields();
        (*fields)["x_forwarded_for"] = v;
        log_format->mutable_json_format_options()->set_sort_properties(true);
        access_log_config_to_clobber->mutable_typed_config()->PackFrom(access_log_config);
      });
  testRouterDownstreamDisconnectBeforeRequestComplete();
  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("x_forwarded_for\":\"\\u00ec"));
}
} // namespace Envoy
