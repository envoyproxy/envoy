#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using namespace std::chrono_literals;

constexpr absl::string_view kFc2Flag =
    "envoy.reloadable_features.async_client_downstream_flow_control2";
constexpr absl::string_view kFc3Flag = "envoy.reloadable_features.grpc_side_stream_flow_control_3";

constexpr absl::string_view kSidestreamPaused =
    "cluster.ext_proc_server_0.upstream_flow_control_paused_reading_total";
constexpr absl::string_view kSidestreamResumed =
    "cluster.ext_proc_server_0.upstream_flow_control_resumed_reading_total";
constexpr absl::string_view kSidestreamBackedUp =
    "cluster.ext_proc_server_0.upstream_flow_control_backed_up_total";
constexpr absl::string_view kUpstreamPaused =
    "cluster.cluster_0.upstream_flow_control_paused_reading_total";
constexpr absl::string_view kDownstreamPaused =
    "http.config_test.downstream_flow_control_paused_reading_total";
constexpr absl::string_view kDownstreamResumed =
    "http.config_test.downstream_flow_control_resumed_reading_total";
constexpr absl::string_view kMessageTimeouts = "http.config_test.ext_proc.message_timeouts";
constexpr absl::string_view kIdleTimeouts = "http.config_test.downstream_rq_idle_timeout";
constexpr absl::string_view kFlushTimeouts = "http2.tx_flush_timeout";

// Per-connection buffer limit for the listener and every cluster (including the ext_proc
// cluster, which is copied from cluster_0).
constexpr uint32_t kBufferLimit = 16 * 1024;
// The client's HTTP/2 receive window: the protocol minimum, so a client that does not read
// pushes back on Envoy after 64 KiB.
constexpr uint32_t kClientStreamWindow = 65535;

class ExtProcSidestreamPushbackDeadlockTest : public ExtProcIntegrationTest {
protected:
  // Flow control 2 stays on in both runs, so the only difference between the runs is flow
  // control 3.
  void setSidestreamFlowControl(bool flow_control_3_enabled) {
    const std::string fc3_value = flow_control_3_enabled ? "true" : "false";
    scoped_runtime_.mergeValues(
        {{std::string(kFc2Flag), "true"}, {std::string(kFc3Flag), fc3_value}});
    config_helper_.addRuntimeOverride(kFc2Flag, "true");
    config_helper_.addRuntimeOverride(kFc3Flag, fc3_value);
  }

  // Starts a POST from a client that does not read its response until it has finished its
  // upload. The request stream is read-disabled on the client, so the client never returns
  // flow-control credit for response data, but it keeps processing WINDOW_UPDATE frames from
  // Envoy, so its upload is limited only by Envoy's own flow control.
  IntegrationStreamDecoderPtr startRequestFromClientThatReadsLast() {
    envoy::config::core::v3::Http2ProtocolOptions http2_options =
        ::Envoy::Http2::Utility::initializeAndValidateOptions(
            envoy::config::core::v3::Http2ProtocolOptions())
            .value();
    http2_options.mutable_initial_stream_window_size()->set_value(kClientStreamWindow);
    codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    headers.setMethod("POST");
    auto encoder_decoder = codec_client_->startRequest(headers);
    request_encoder_ = &encoder_decoder.first;
    request_encoder_->getStream().readDisable(true);
    return std::move(encoder_decoder.second);
  }

  uint64_t counterValue(absl::string_view name) {
    Stats::CounterSharedPtr counter = test_server_->counter(std::string(name));
    return counter == nullptr ? 0 : counter->value();
  }

  std::string flowControlSummary() {
    return absl::StrCat("\n  side stream read paused/resumed: ", counterValue(kSidestreamPaused),
                        "/", counterValue(kSidestreamResumed),
                        "\n  side stream write backed up: ", counterValue(kSidestreamBackedUp),
                        "\n  upstream read paused: ", counterValue(kUpstreamPaused),
                        "\n  downstream read paused/resumed: ", counterValue(kDownstreamPaused),
                        "/", counterValue(kDownstreamResumed),
                        "\n  ext_proc message timeouts: ", counterValue(kMessageTimeouts),
                        "\n  downstream stream idle timeouts: ", counterValue(kIdleTimeouts),
                        "\n  downstream HTTP/2 flush timeouts: ", counterValue(kFlushTimeouts));
  }

  void runRequestBodyScanningWithSlowReader(bool flow_control_3_enabled) {
    constexpr uint64_t kChunkSize = 4 * 1024;
    // Enough chunks to overflow ext_proc's request chunk queue (the 16 KiB buffer limit) if the
    // server's replies are not delivered.
    constexpr int kChunksAfterPause = 10;
    constexpr uint64_t kRequestBodySize = kChunkSize * (1 + kChunksAfterPause);
    constexpr uint64_t kResponseBodySize = 256 * 1024;

    setSidestreamFlowControl(flow_control_3_enabled);
    config_helper_.setBufferLimits(kBufferLimit, kBufferLimit);
    auto* processing_mode = proto_config_.mutable_processing_mode();
    processing_mode->set_request_header_mode(ProcessingMode::SKIP);
    processing_mode->set_request_body_mode(ProcessingMode::STREAMED);
    processing_mode->set_request_trailer_mode(ProcessingMode::SKIP);
    processing_mode->set_response_header_mode(ProcessingMode::SKIP);
    processing_mode->set_response_body_mode(ProcessingMode::NONE);
    processing_mode->set_response_trailer_mode(ProcessingMode::SKIP);
    // Long enough that a server answering immediately never trips it.
    proto_config_.mutable_message_timeout()->set_seconds(2);
    initializeConfig();
    HttpIntegrationTest::initialize();

    IntegrationStreamDecoderPtr response = startRequestFromClientThatReadsLast();

    // The first request chunk opens the side stream, and the server answers it.
    codec_client_->sendData(*request_encoder_, kChunkSize, false);
    processRequestBodyMessage(*grpc_upstreams_[0], /*first_message=*/true, std::nullopt);

    // Request headers are not processed, so the upstream sees them right away, followed by the
    // first chunk once ext_proc has its reply.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, kChunkSize));

    // The upstream starts a large streamed response before the upload is done. The client is
    // not reading it, so the client-side buffer goes above its high watermark and stays there.
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(kResponseBodySize, false);
    test_server_->waitForCounter(std::string(kUpstreamPaused), testing::Ge(1));
    // Even with flow control 3 enabled, Envoy must not pause reading the ext_proc side stream when
    // response_body_mode is NONE.
    EXPECT_EQ(0, counterValue(kSidestreamPaused));

    // The client keeps uploading.
    for (int i = 0; i < kChunksAfterPause; ++i) {
      codec_client_->sendData(*request_encoder_, kChunkSize, i == kChunksAfterPause - 1);
    }

    // The server answers every message it receives, immediately.
    int messages_answered = 1;
    bool saw_end_of_stream = false;
    while (!saw_end_of_stream) {
      ProcessingRequest request;
      if (!processor_stream_->waitForGrpcMessage(*dispatcher_, request, 1s)) {
        break;
      }
      ASSERT_TRUE(request.has_request_body());
      saw_end_of_stream = request.request_body().end_of_stream();
      ProcessingResponse reply;
      reply.mutable_request_body();
      processor_stream_->sendGrpcMessage(reply);
      ++messages_answered;
    }

    // Correct behavior: every request byte reaches the upstream.
    const bool upload_delivered =
        upstream_request_->waitForData(*dispatcher_, kRequestBodySize, 5s);
    EXPECT_TRUE(upload_delivered)
        << "Upload stalled with FC3 " << (flow_control_3_enabled ? "on" : "off")
        << ": the upstream got " << upstream_request_->bodyLength() << " of " << kRequestBodySize
        << " request body bytes. The ext_proc server received and answered " << messages_answered
        << " request_body messages (end_of_stream seen: " << saw_end_of_stream
        << "), but Envoy never delivered the replies." << flowControlSummary();
    EXPECT_EQ(0, counterValue(kMessageTimeouts))
        << "ext_proc timed out waiting for replies that the server had already sent."
        << flowControlSummary();
    EXPECT_EQ(0, counterValue(kSidestreamPaused));
    if (!upload_delivered) {
      return;
    }

    // Once its upload is done, the client reads the whole response.
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeData(0, true);
    request_encoder_->getStream().readDisable(false);
    verifyDownstreamResponse(*response, 200);
    EXPECT_EQ(kResponseBodySize, response->body().size());
    EXPECT_EQ(0, counterValue(kMessageTimeouts));
  }

  // Answers one ext_proc message the way a pass-through server does: header and trailer
  // messages get an empty (CONTINUE) reply, and body chunks are echoed back unchanged as
  // FULL_DUPLEX_STREAMED replies. Returns the number of bytes written to the side stream.
  uint64_t answerPassThrough(const ProcessingRequest& request) {
    ProcessingResponse reply;
    auto echo = [](const envoy::service::ext_proc::v3::HttpBody& body,
                   envoy::service::ext_proc::v3::BodyResponse& body_response) {
      auto* streamed_response =
          body_response.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
      streamed_response->set_body(body.body());
      streamed_response->set_end_of_stream(body.end_of_stream());
    };
    if (request.has_request_headers()) {
      reply.mutable_request_headers();
    } else if (request.has_request_body()) {
      echo(request.request_body(), *reply.mutable_request_body());
    } else if (request.has_request_trailers()) {
      reply.mutable_request_trailers();
    } else if (request.has_response_headers()) {
      reply.mutable_response_headers();
    } else if (request.has_response_body()) {
      echo(request.response_body(), *reply.mutable_response_body());
    } else if (request.has_response_trailers()) {
      reply.mutable_response_trailers();
    }
    processor_stream_->sendGrpcMessage(reply);
    // gRPC frame header + message.
    return 5 + reply.ByteSizeLong();
  }

  void runFullDuplexWithPassThroughServer(bool flow_control_3_enabled) {
    constexpr uint64_t kChunkSize = 16 * 1024;
    constexpr int kRequestChunks = 32;
    constexpr int kChunksBeforeResponse = 2;
    constexpr uint64_t kRequestBodySize = kChunkSize * kRequestChunks;
    constexpr uint64_t kResponseBodySize = 256 * 1024;
    // HTTP/2 receive window on both ends of the side stream.
    constexpr uint32_t kSidestreamWindow = 65535;

    setSidestreamFlowControl(flow_control_3_enabled);
    config_helper_.setBufferLimits(kBufferLimit, kBufferLimit);
    // The ext_proc server's receive window: once the server stops reading, Envoy can send it
    // at most 64 KiB more.
    upstreamConfig().http2_options_.mutable_initial_stream_window_size()->set_value(
        kSidestreamWindow);
    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      hcm.mutable_stream_idle_timeout()->set_seconds(5);
    });
    auto* processing_mode = proto_config_.mutable_processing_mode();
    processing_mode->set_request_header_mode(ProcessingMode::SEND);
    processing_mode->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_request_trailer_mode(ProcessingMode::SEND);
    processing_mode->set_response_header_mode(ProcessingMode::SEND);
    processing_mode->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_response_trailer_mode(ProcessingMode::SEND);
    initializeConfig();
    // Envoy's receive window for the side stream: once Envoy stops reading, the server can write
    // at most 64 KiB more. Runs after initializeConfig() has created the ext_proc cluster.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      for (auto& cluster : *bootstrap.mutable_static_resources()->mutable_clusters()) {
        if (cluster.name() == "ext_proc_server_0") {
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http2_protocol_options()
              ->mutable_initial_stream_window_size()
              ->set_value(kSidestreamWindow);
          ConfigHelper::setProtocolOptions(cluster, protocol_options);
        }
      }
    });
    HttpIntegrationTest::initialize();

    Event::TestTimeSystem::RealTimeBound bound(30s);
    IntegrationStreamDecoderPtr response = startRequestFromClientThatReadsLast();
    int chunks_sent = 0;
    auto client_upload = [&](int up_to_chunk) {
      while (chunks_sent < up_to_chunk) {
        ++chunks_sent;
        codec_client_->sendData(*request_encoder_, kChunkSize, chunks_sent == kRequestChunks);
      }
    };

    // The pass-through server. It cannot observe its own HTTP/2 send window, so it uses Envoy's
    // side-stream read pause as the signal: while Envoy is not reading the side stream, at most
    // kSidestreamWindow bytes of replies can be in flight, and after that the server's next
    // write would block, so it stops reading. It starts reading again as soon as Envoy does.
    bool server_blocked = false;
    bool server_ever_blocked = false;
    uint64_t replies_written_while_paused = 0;
    // Processes at most one message; returns true if it did.
    auto server_step = [&](std::chrono::milliseconds timeout) -> bool {
      const bool envoy_reading_sidestream =
          counterValue(kSidestreamPaused) == counterValue(kSidestreamResumed);
      if (envoy_reading_sidestream) {
        replies_written_while_paused = 0;
        if (server_blocked) {
          processor_stream_->readDisable(false);
          server_blocked = false;
        }
      } else if (!server_blocked && replies_written_while_paused >= kSidestreamWindow) {
        processor_stream_->readDisable(true);
        server_blocked = true;
        server_ever_blocked = true;
      }
      if (server_blocked) {
        dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
        timeSystem().advanceTimeWait(timeout);
        return false;
      }
      ProcessingRequest request;
      if (!processor_stream_->waitForGrpcMessage(*dispatcher_, request, timeout)) {
        return false;
      }
      const uint64_t written = answerPassThrough(request);
      if (!envoy_reading_sidestream) {
        replies_written_while_paused += written;
      }
      return true;
    };

    // The client sends its headers and the first chunks. The server answers the request headers
    // and echoes the chunks to the upstream.
    client_upload(kChunksBeforeResponse);
    ProcessingRequest request_headers;
    waitForFirstMessage(*grpc_upstreams_[0], request_headers);
    if (HasFatalFailure()) {
      return;
    }
    ASSERT_TRUE(request_headers.has_request_headers());
    processor_stream_->startGrpcStream();
    answerPassThrough(request_headers);
    // In FULL_DUPLEX_STREAMED mode Envoy forwards the request headers together with the first
    // echoed body chunk, so keep the server running while waiting for the upstream.
    while ((upstream_request_ == nullptr ||
            upstream_request_->bodyLength() < kChunksBeforeResponse * kChunkSize) &&
           bound.withinBound()) {
      server_step(50ms);
      if (fake_upstream_connection_ == nullptr) {
        (void)fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                        50ms);
      }
      if (fake_upstream_connection_ != nullptr && upstream_request_ == nullptr) {
        (void)fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_, 50ms);
      }
    }
    ASSERT_NE(nullptr, upstream_request_);
    ASSERT_EQ(kChunksBeforeResponse * kChunkSize, upstream_request_->bodyLength());

    // The upstream answers before the upload is done. The server processes the response until
    // nothing more arrives: the client is not reading, so the client-side buffer goes above its
    // high watermark and the router stops reading the upstream.
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(kResponseBodySize, false);
    test_server_->waitForCounter(std::string(kUpstreamPaused), testing::Ge(1), 10s,
                                 dispatcher_.get());
    // Process the response until the server has been idle for 500 ms (or is blocked).
    for (int idle_steps = 0; idle_steps < 5 && !server_blocked && bound.withinBound();) {
      idle_steps = server_step(100ms) ? 0 : idle_steps + 1;
    }
    // While client end_stream has not been received, flow control 3 must not pause the side
    // stream.
    EXPECT_EQ(0, counterValue(kSidestreamPaused));

    // The client continues uploading while still not reading the response (downstream buffer is
    // above its high watermark). Before the final chunk (end_stream = true), flow control 3 must
    // not pause the side stream, so all chunks up to kRequestChunks - 1 are echoed to the
    // upstream.
    client_upload(kRequestChunks - 1);
    while (upstream_request_->bodyLength() < (kRequestChunks - 1) * kChunkSize &&
           !response->reset() && bound.withinBound()) {
      server_step(100ms);
    }
    EXPECT_EQ((kRequestChunks - 1) * kChunkSize, upstream_request_->bodyLength());
    EXPECT_EQ(0, counterValue(kSidestreamPaused));

    // The client sends the final chunk (end_stream = true). With flow control 3 enabled, Envoy now
    // enables downstream watermark backpressure on the side stream and immediately pauses reading
    // it because the downstream buffer is still above its high watermark. Once the client starts
    // reading the response, the downstream buffer drains, the side stream resumes, and the upload
    // completes.
    client_upload(kRequestChunks);
    if (flow_control_3_enabled) {
      test_server_->waitForCounter(std::string(kSidestreamPaused), testing::Ge(1), 10s,
                                   dispatcher_.get());
    }
    request_encoder_->getStream().readDisable(false);
    while (!upstream_request_->complete() && !response->reset() && bound.withinBound()) {
      server_step(100ms);
    }

    // Correct behavior: the upload completes...
    const bool upload_completed = upstream_request_->complete();
    EXPECT_TRUE(upload_completed) << "Upload deadlocked with FC3 "
                                  << (flow_control_3_enabled ? "on" : "off")
                                  << ": the upstream got " << upstream_request_->bodyLength()
                                  << " of " << kRequestBodySize
                                  << " request body bytes. Pass-through server stopped reading: "
                                  << server_ever_blocked
                                  << ". Client stream reset: " << response->reset() << "."
                                  << flowControlSummary();
    EXPECT_EQ(0, counterValue(kIdleTimeouts)) << flowControlSummary();
    EXPECT_EQ(0, counterValue(kFlushTimeouts)) << flowControlSummary();
    if (flow_control_3_enabled) {
      EXPECT_GE(counterValue(kSidestreamPaused), 1);
    } else {
      EXPECT_EQ(0, counterValue(kSidestreamPaused));
      EXPECT_FALSE(server_ever_blocked);
    }
    if (!upload_completed) {
      return;
    }
    EXPECT_EQ(kRequestBodySize, upstream_request_->bodyLength());

    // ...and then the client receives the whole response.
    upstream_request_->encodeData(0, true);
    while (!response->complete() && !response->reset() && bound.withinBound()) {
      server_step(100ms);
    }
    ASSERT_TRUE(response->complete()) << flowControlSummary();
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(kResponseBodySize, response->body().size());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcSidestreamPushbackDeadlockTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Flow control 3: grpc_side_stream_flow_control_3, i.e., downstream backpressure on the
// ext_proc side stream.
TEST_P(ExtProcSidestreamPushbackDeadlockTest, RequestBodyScanningWithSlowReaderFc3Off) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "FC3 applies only to Envoy gRPC side streams.";
  }
  runRequestBodyScanningWithSlowReader(/*flow_control_3_enabled=*/false);
}

TEST_P(ExtProcSidestreamPushbackDeadlockTest, RequestBodyScanningWithSlowReaderFc3On) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "FC3 applies only to Envoy gRPC side streams.";
  }
  runRequestBodyScanningWithSlowReader(/*flow_control_3_enabled=*/true);
}

// Control run: without flow control 3, FULL_DUPLEX_STREAMED in both directions works with a
// pass-through server and a client that reads only after its upload.
TEST_P(ExtProcSidestreamPushbackDeadlockTest, FullDuplexWithPassThroughServerFc3Off) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "FC3 applies only to Envoy gRPC side streams.";
  }
  runFullDuplexWithPassThroughServer(/*flow_control_3_enabled=*/false);
}

TEST_P(ExtProcSidestreamPushbackDeadlockTest, FullDuplexWithPassThroughServerFc3On) {
  if (!IsEnvoyGrpc()) {
    GTEST_SKIP() << "FC3 applies only to Envoy gRPC side streams.";
  }
  runFullDuplexWithPassThroughServer(/*flow_control_3_enabled=*/true);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
