#include "test/integration/h1_fuzz.h"

#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {

void H1FuzzIntegrationTest::replay(const test::integration::CaptureFuzzTestCase& input) {
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

} // namespace Envoy
