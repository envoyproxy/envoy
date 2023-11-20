#include "test/integration/h2_fuzz.h"

#include <functional>

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/logger.h"

#include "test/test_common/environment.h"

namespace Envoy {

using namespace Envoy::Http::Http2;

namespace {

static Http2Frame::HeadersFlags
unifyHeadersFlags(const Protobuf::RepeatedField<int>& headers_flags) {
  int unified_flags = 0;
  for (const auto& flag : headers_flags) {
    unified_flags |= flag;
  }
  return static_cast<Http2Frame::HeadersFlags>(unified_flags);
}

} // namespace

void H2FuzzIntegrationTest::sendFrame(const test::integration::H2TestFrame& proto_frame,
                                      std::function<void(const Http2Frame&)> write_func) {
  Http2Frame h2_frame;
  switch (proto_frame.frame_type_case()) {
  case test::integration::H2TestFrame::kPing:
    ENVOY_LOG_MISC(trace, "Sending ping frame");
    h2_frame = Http2Frame::makePingFrame(proto_frame.ping().data());
    break;
  case test::integration::H2TestFrame::kSettings: {
    const Http2Frame::SettingsFlags settings_flags =
        static_cast<Http2Frame::SettingsFlags>(proto_frame.settings().flags());
    ENVOY_LOG_MISC(trace, "Sending settings frame");
    h2_frame = Http2Frame::makeEmptySettingsFrame(settings_flags);
    break;
  }
  case test::integration::H2TestFrame::kHeaders: {
    const Http2Frame::HeadersFlags headers_flags = unifyHeadersFlags(proto_frame.headers().flags());
    const uint32_t stream_idx = proto_frame.headers().stream_index();
    ENVOY_LOG_MISC(trace, "Sending headers frame");
    h2_frame = Http2Frame::makeEmptyHeadersFrame(stream_idx, headers_flags);
    break;
  }
  case test::integration::H2TestFrame::kContinuation: {
    const Http2Frame::HeadersFlags headers_flags =
        unifyHeadersFlags(proto_frame.continuation().flags());
    const uint32_t stream_idx = proto_frame.continuation().stream_index();
    ENVOY_LOG_MISC(trace, "Sending continuation frame");
    h2_frame = Http2Frame::makeEmptyContinuationFrame(stream_idx, headers_flags);
    break;
  }
  case test::integration::H2TestFrame::kData: {
    const Http2Frame::DataFlags data_flags =
        static_cast<Http2Frame::DataFlags>(proto_frame.data().flags());
    const uint32_t stream_idx = proto_frame.data().stream_index();
    ENVOY_LOG_MISC(trace, "Sending data frame");
    h2_frame = Http2Frame::makeEmptyDataFrame(stream_idx, data_flags);
    break;
  }
  case test::integration::H2TestFrame::kPriority: {
    const uint32_t stream_idx = proto_frame.priority().stream_index();
    const uint32_t dependent_idx = proto_frame.priority().dependent_index();
    ENVOY_LOG_MISC(trace, "Sending priority frame");
    h2_frame = Http2Frame::makePriorityFrame(stream_idx, dependent_idx);
    break;
  }
  case test::integration::H2TestFrame::kPushPromise: {
    const Http2Frame::HeadersFlags headers_flags =
        unifyHeadersFlags(proto_frame.push_promise().flags());
    const uint32_t stream_idx = proto_frame.push_promise().stream_index();
    const uint32_t promised_stream_idx = proto_frame.push_promise().promised_stream_index();
    ENVOY_LOG_MISC(trace, "Sending push promise frame");
    h2_frame =
        Http2Frame::makeEmptyPushPromiseFrame(stream_idx, promised_stream_idx, headers_flags);
    break;
  }
  case test::integration::H2TestFrame::kResetStream: {
    const uint32_t stream_idx = proto_frame.reset_stream().stream_index();
    const Http2Frame::ErrorCode error_code =
        static_cast<Http2Frame::ErrorCode>(proto_frame.reset_stream().error_code());
    ENVOY_LOG_MISC(trace, "Sending reset stream frame");
    h2_frame = Http2Frame::makeResetStreamFrame(stream_idx, error_code);
    break;
  }
  case test::integration::H2TestFrame::kGoAway: {
    const uint32_t last_stream_idx = proto_frame.go_away().last_stream_index();
    const Http2Frame::ErrorCode error_code =
        static_cast<Http2Frame::ErrorCode>(proto_frame.go_away().error_code());
    ENVOY_LOG_MISC(trace, "Sending go-away frame");
    h2_frame = Http2Frame::makeEmptyGoAwayFrame(last_stream_idx, error_code);
    break;
  }
  case test::integration::H2TestFrame::kWindowUpdate: {
    const uint32_t stream_idx = proto_frame.window_update().stream_index();
    const uint32_t increment = proto_frame.window_update().increment();
    ENVOY_LOG_MISC(trace, "Sending windows_update frame");
    h2_frame = Http2Frame::makeWindowUpdateFrame(stream_idx, increment);
    break;
  }
  case test::integration::H2TestFrame::kMalformedRequest: {
    const uint32_t stream_idx = proto_frame.malformed_request().stream_index();
    ENVOY_LOG_MISC(trace, "Sending malformed_request frame");
    h2_frame = Http2Frame::makeMalformedRequest(stream_idx);
    break;
  }
  case test::integration::H2TestFrame::kMalformedRequestWithZerolenHeader: {
    const uint32_t stream_idx = proto_frame.malformed_request_with_zerolen_header().stream_index();
    const absl::string_view host = proto_frame.malformed_request_with_zerolen_header().host();
    const absl::string_view path = proto_frame.malformed_request_with_zerolen_header().path();
    ENVOY_LOG_MISC(trace, "Sending malformed_request_with_zerolen_header");
    h2_frame = Http2Frame::makeMalformedRequestWithZerolenHeader(stream_idx, host, path);
    break;
  }
  case test::integration::H2TestFrame::kRequest: {
    const uint32_t stream_idx = proto_frame.request().stream_index();
    const absl::string_view host = proto_frame.request().host();
    const absl::string_view path = proto_frame.request().path();
    ENVOY_LOG_MISC(trace, "Sending request");
    h2_frame = Http2Frame::makeRequest(stream_idx, host, path);
    break;
  }
  case test::integration::H2TestFrame::kPostRequest: {
    const uint32_t stream_idx = proto_frame.post_request().stream_index();
    const absl::string_view host = proto_frame.post_request().host();
    const absl::string_view path = proto_frame.post_request().path();
    ENVOY_LOG_MISC(trace, "Sending post request");
    h2_frame = Http2Frame::makePostRequest(stream_idx, host, path);
    break;
  }
  case test::integration::H2TestFrame::kMetadata: {
    const Http2Frame::MetadataFlags metadata_flags =
        static_cast<Http2Frame::MetadataFlags>(proto_frame.metadata().flags());
    const uint32_t stream_idx = proto_frame.metadata().stream_index();
    Http::MetadataMap metadata_map;
    for (const auto& metadataPair : proto_frame.metadata().metadata().metadata()) {
      metadata_map.insert(metadataPair);
    }
    ENVOY_LOG_MISC(trace, "Sending metadata frame.");
    h2_frame =
        Http2Frame::makeMetadataFrameFromMetadataMap(stream_idx, metadata_map, metadata_flags);
    break;
  }
  case test::integration::H2TestFrame::kStatus: {
    const std::string status = proto_frame.status().status();
    const uint32_t stream_idx = proto_frame.status().stream_index();
    ENVOY_LOG_MISC(trace, "Sending status frame");
    h2_frame = Http2Frame::makeHeadersFrameWithStatus(status, stream_idx);
    break;
  }
  case test::integration::H2TestFrame::kGeneric: {
    const absl::string_view frame_bytes = proto_frame.generic().frame_bytes();
    ENVOY_LOG_MISC(trace, "Sending generic frame");
    h2_frame = Http2Frame::makeGenericFrame(frame_bytes);
    break;
  }
  default:
    ENVOY_LOG_MISC(debug, "Proto-frame not supported!");
    break;
  }

  write_func(h2_frame);
}

void H2FuzzIntegrationTest::replay(const test::integration::H2CaptureFuzzTestCase& input,
                                   bool ignore_response) {
  struct Init {
    Init(H2FuzzIntegrationTest* test) { test->initialize(); }
  };
  PERSISTENT_FUZZ_VAR(Init, initialized, (this));
  UNREFERENCED_PARAMETER(initialized);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  FakeRawConnectionPtr fake_upstream_connection;
  bool stop_further_inputs = false;
  bool preamble_sent = false;
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
    case test::integration::Event::kDownstreamSendEvent: {
      auto downstream_write_func = [&](const Http2Frame& h2_frame) -> void {
        ASSERT_TRUE(tcp_client->write(std::string(h2_frame), false, false));
      };
      if (!preamble_sent) {
        // Start H2 session - send hello string
        ASSERT_TRUE(tcp_client->write(Http2Frame::Preamble, false, false));
        preamble_sent = true;
      }
      for (auto& frame : event.downstream_send_event().h2_frames()) {
        if (!tcp_client->connected()) {
          ENVOY_LOG_MISC(debug,
                         "Disconnected, avoiding sending data, no further event processing.");
          break;
        }

        ENVOY_LOG_MISC(trace, "sending downstream frame");
        sendFrame(frame, downstream_write_func);
      }
      break;
    }
    case test::integration::Event::kUpstreamSendEvent: {
      if (ignore_response) {
        break;
      }
      if (fake_upstream_connection == nullptr) {
        if (!fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection, max_wait_ms_)) {
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
      {
        auto upstream_write_func = [&](const Http2Frame& h2_frame) -> void {
          AssertionResult result = fake_upstream_connection->write(std::string(h2_frame));
          RELEASE_ASSERT(result, result.message());
        };
        for (auto& frame : event.upstream_send_event().h2_frames()) {
          if (!fake_upstream_connection->connected()) {
            ENVOY_LOG_MISC(
                debug,
                "Upstream disconnected, avoiding sending data, no further event processing.");
            stop_further_inputs = true;
            break;
          }

          ENVOY_LOG_MISC(trace, "sending upstream frame");
          sendFrame(frame, upstream_write_func);
        }
      }
      break;
    }
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
    AssertionResult result = fake_upstream_connection->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
  if (tcp_client->connected()) {
    tcp_client->close();
  }
}

} // namespace Envoy
