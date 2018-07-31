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

  void replay(const test::integration::CaptureFuzzTestCase& input) {
    initialize();
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    FakeRawConnectionPtr fake_upstream_connection;
    for (int i = 0; i < input.events().size(); ++i) {
      const auto& event = input.events(i);
      ENVOY_LOG_MISC(debug, "Processing event: {}", event.DebugString());
      // If we're disconnected, we fail out.
      if (!tcp_client->connected()) {
        ENVOY_LOG_MISC(debug, "Disconnected, no further event processing.");
        break;
      }
      switch (event.event_selector_case()) {
      case test::integration::Event::kDownstreamSendBytes:
        tcp_client->write(event.downstream_send_bytes(), false, false);
        break;
      case test::integration::Event::kDownstreamRecvBytes:
        // TODO(htuch): Should we wait for some data?
        break;
      case test::integration::Event::kUpstreamSendBytes:
        if (fake_upstream_connection == nullptr) {
          if (!fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection, max_wait_ms_)) {
            // If we timed out, we fail out.
            tcp_client->close();
            return;
          }
        }
        // If we're no longer connected, we're done.
        if (!fake_upstream_connection->connected()) {
          tcp_client->close();
          return;
        }
        {
          AssertionResult result = fake_upstream_connection->write(event.upstream_send_bytes());
          RELEASE_ASSERT(result, result.message());
        }
        break;
      case test::integration::Event::kUpstreamRecvBytes:
        // TODO(htuch): Should we wait for some data?
        break;
      default:
        // Maybe nothing is set?
        break;
      }
    }
    if (fake_upstream_connection != nullptr) {
      if (fake_upstream_connection->connected()) {
        AssertionResult result = fake_upstream_connection->close();
        RELEASE_ASSERT(result, result.message());
      }
      AssertionResult result = fake_upstream_connection->waitForDisconnect(true);
      RELEASE_ASSERT(result, result.message());
    }
    tcp_client->close();
  }

  // We bound timeouts to ensure we're running at least at around 100s execs/second. Any slower is
  // considered too slow for fuzzing, if we make the wait too short we risk excess flaking.
  const std::chrono::milliseconds max_wait_ms_{10};
};

// Fuzz the H1 processing pipeline.
DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  RELEASE_ASSERT(TestEnvironment::getIpVersionsForTest().size() > 0, "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  H1FuzzIntegrationTest h1_fuzz_integration_test(ip_version);
  h1_fuzz_integration_test.replay(input);
}

} // namespace Envoy
