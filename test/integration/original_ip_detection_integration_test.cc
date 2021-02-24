#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Formatter {

class OriginalIPDetectionIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  OriginalIPDetectionIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, Network::Address::IpVersion::v4) {}
};

TEST_F(OriginalIPDetectionIntegrationTest, HeaderBasedDetection) {
  useAccessLog("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig config;
        config.set_header_name("x-cdn-detected-ip");

        auto* extension = hcm.add_original_ip_detection_extensions();
        extension->set_name("envoy.http.original_ip_detection.custom_header");
        extension->mutable_typed_config()->PackFrom(config);

        hcm.mutable_use_remote_address()->set_value(false);
      });
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\nx-cdn-detected-ip: 9.9.9.9\r\n\r\n",
      &response, true);
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("9.9.9.9"));
}

} // namespace Formatter
} // namespace Envoy
