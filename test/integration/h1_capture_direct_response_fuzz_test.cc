#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/capture_fuzz.pb.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {

class H1FuzzIntegrationTest : public HttpIntegrationTest {
public:
  H1FuzzIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, version) {}

  void initialize() override {
    const std::string body = "Response body";
    const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", body);
    static const std::string domain("direct.example.com");
    static const std::string prefix("/");
    static const Http::Code status(Http::Code::OK);
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->add_virtual_hosts();
          virtual_host->set_name(domain);

          virtual_host->add_domains(domain);
          virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
          virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
              static_cast<uint32_t>(status));
          virtual_host->mutable_routes(0)->mutable_direct_response()->mutable_body()->set_filename(
              file_path);
        });
    HttpIntegrationTest::initialize();
  }

  void replay(const test::integration::CaptureFuzzTestCase& input) {
    initialize();
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    for (int i = 0; i < input.events().size(); ++i) {
      const auto& event = input.events(i);
      ENVOY_LOG_MISC(debug, "Processing event: {}", event.DebugString());
      if (!tcp_client->connected()) {
        EXPECT_TRUE(tcp_client->connected());
        break;
      }
      switch (event.event_selector_case()) {
      case test::integration::Event::kDownstreamSendBytes:
        tcp_client->write(event.downstream_send_bytes(), false, false);
        break;
      case test::integration::Event::kDownstreamRecvBytes:
        break;
      default:
        break;
      }
      // Upstream data fuzzing isn't considered since direct response mode is enabled
    }
    tcp_client->close();
  }
  const std::chrono::milliseconds max_wait_ms_{10};
}; // namespace Envoy

// Fuzz the H1 processing pipeline.
DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  RELEASE_ASSERT(TestEnvironment::getIpVersionsForTest().size() > 0);
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  H1FuzzIntegrationTest h1_fuzz_integration_test(ip_version);
  h1_fuzz_integration_test.replay(input);
}

} // namespace Envoy
