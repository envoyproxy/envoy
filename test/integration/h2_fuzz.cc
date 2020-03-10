#include "test/integration/h2_fuzz.h"

#include <functional>

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/logger.h"

//#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {

using namespace Envoy::Http::Http2;

void H2FuzzIntegrationTest::replay(const test::integration::H2CaptureFuzzTestCase& input) {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  FakeRawConnectionPtr fake_upstream_connection;
  bool stop_further_inputs = false;
  for (int i = 0; i < input.events().size(); ++i) {
    if (stop_further_inputs) {
      break;
    }
    const auto& event = input.events(i);
    ENVOY_LOG_MISC(debug, "Processing event: {}", event.DebugString());
    // If we're disconnected, we fail out.
    if (!tcp_client->connected()) {
      ENVOY_LOG_MISC(debug, "Disconnected, no further event processing.");
      break;
    }
    switch (event.event_selector_case()) {
    // TODO(adip): Add ability to test complex interactions when receiving data
    case test::integration::Event::kDownstreamSendEvent:
      // Start H2 session - send hello string
      tcp_client->write(Http2Frame::Preamble, false, false);
      for (auto& frame_func_idx : event.downstream_send_event().frames()) {
        if (!tcp_client->connected()) {
          ENVOY_LOG_MISC(debug,
                         "Disconnected, avoiding sending data, no further event processing.");
          break;
        }

        const Http2Frame frame = frameMakers[frame_func_idx]();
        ENVOY_LOG_MISC(trace, "sending downstream frame idx: {}", frame_func_idx);
        tcp_client->write(std::string(frame), false, false);
      }
      break;
    case test::integration::Event::kUpstreamSendEvent:
      if (fake_upstream_connection == nullptr) {
        // TODO(adip): The H1 fuzzer uses a short max_wait_ms_ when calling this function. When
        // trying to do so with H2, an ABORT happens in FileEventImpl::assignEvents
        if (!fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection)) {
          // If we timed out, we fail out.
          if (tcp_client->connected()) {
            tcp_client->close();
          }
          stop_further_inputs = true;
          break;
        }
      }
      // If we're no longer connected, we're done.
      if (!fake_upstream_connection->connected()) {
        if (tcp_client->connected()) {
          tcp_client->close();
        }
        stop_further_inputs = true;
        break;
      }
      for (auto& frame_func_idx : event.upstream_send_event().frames()) {
        if (!fake_upstream_connection->connected()) {
          ENVOY_LOG_MISC(
              debug, "Upstream disconnected, avoiding sending data, no further event processing.");
          stop_further_inputs = true;
          break;
        }

        const Http2Frame frame = frameMakers[frame_func_idx]();
        ENVOY_LOG_MISC(trace, "sending upstream frame idx: {}", frame_func_idx);
        AssertionResult result = fake_upstream_connection->write(std::string(frame));
        RELEASE_ASSERT(result, result.message());
      }
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
  if (tcp_client->connected()) {
    tcp_client->close();
  }
}

} // namespace Envoy
