#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Formatter {

class OriginalIPDetectionIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  OriginalIPDetectionIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void runTest(const std::string& ip) {
    autonomous_upstream_ = true;
    useAccessLog("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig
              config;
          config.set_header_name("x-cdn-detected-ip");

          auto* extension = hcm.add_original_ip_detection_extensions();
          extension->set_name("envoy.http.original_ip_detection.custom_header");
          extension->mutable_typed_config()->PackFrom(config);

          hcm.mutable_use_remote_address()->set_value(false);
        });
    initialize();
    auto raw_http =
        fmt::format("GET / HTTP/1.1\r\nHost: host\r\nx-cdn-detected-ip: {}\r\n\r\n", ip);
    std::string response;
    sendRawHttpAndWaitForResponse(lookupPort("http"), raw_http.c_str(), &response, true);
    std::string log = waitForAccessLog(access_log_name_);
    EXPECT_THAT(log, HasSubstr(ip));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OriginalIPDetectionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(OriginalIPDetectionIntegrationTest, HeaderBasedDetectionIPv4) { runTest("9.9.9.9"); }

TEST_P(OriginalIPDetectionIntegrationTest, HeaderBasedDetectionIPv6) { runTest("fc00::1"); }

} // namespace Formatter
} // namespace Envoy
