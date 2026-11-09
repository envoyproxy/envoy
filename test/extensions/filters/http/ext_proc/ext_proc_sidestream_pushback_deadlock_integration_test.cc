#include <cstdint>
#include <string>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::HttpBody;

constexpr absl::string_view kFc2Flag =
    "envoy.reloadable_features.async_client_downstream_flow_control2";
constexpr absl::string_view kFc3Flag = "envoy.reloadable_features.grpc_side_stream_flow_control_3";

constexpr absl::string_view kSidestreamResumed =
    "cluster.ext_proc_server_0.upstream_flow_control_resumed_reading_total";
constexpr absl::string_view kMessageTimeouts = "http.config_test.ext_proc.message_timeouts";

constexpr uint32_t kBufferLimit = 16 * 1024;
constexpr uint32_t kClientStreamWindow = 65535;

class ExtProcSidestreamPushbackDeadlockTest : public ExtProcIntegrationTest {
protected:
  void setSidestreamFlowControl(bool flow_control_3_enabled) {
    const std::string fc3_value = flow_control_3_enabled ? "true" : "false";
    scoped_runtime_.mergeValues(
        {{std::string(kFc2Flag), "true"}, {std::string(kFc3Flag), fc3_value}});
    config_helper_.addRuntimeOverride(kFc2Flag, "true");
    config_helper_.addRuntimeOverride(kFc3Flag, fc3_value);
  }

  // Starts a POST with downstream reads disabled until the upload finishes.
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

  // Sends num_chunks request body chunks of chunk_size bytes from the client, setting end_stream on
  // the last chunk when end_stream is true.
  void sendClientRequestChunks(int num_chunks, uint64_t chunk_size, bool end_stream) {
    for (int i = 0; i < num_chunks; ++i) {
      codec_client_->sendData(*request_encoder_, chunk_size, end_stream && (i == num_chunks - 1));
    }
  }

  void waitForUpstreamRequestData(uint64_t expected_bytes) {
    if (!fake_upstream_connection_) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }
    if (!upstream_request_) {
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    }
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expected_bytes));
  }

  // Receives FULL_DUPLEX_STREAMED request body messages from Envoy totaling target_bytes and echoes
  // each chunk back as a streamed_response mutation.
  void echoRequestBytes(uint64_t target_bytes) {
    uint64_t echoed = 0;
    while (echoed < target_bytes) {
      processRequestBodyMessage(*grpc_upstreams_[0], /*first_message=*/false,
                                [&](const HttpBody& body, BodyResponse&) {
                                  echoed += body.body().size();
                                  serverSendBodyRespDuplexStreamed(
                                      /*total_resp_body_msg=*/1, processor_stream_,
                                      body.end_of_stream(), /*response=*/false, body.body());
                                  return false;
                                });
    }
  }

  void runRequestBodyScanningWithSlowReader(bool flow_control_3_enabled) {
    constexpr uint64_t kChunkSize = 4 * 1024;
    constexpr int kChunksAfterPause = 10;
    constexpr uint64_t kRequestBodySize = kChunkSize * (1 + kChunksAfterPause);
    constexpr uint64_t kResponseBodySize = 256 * 1024;

    setSidestreamFlowControl(flow_control_3_enabled);
    config_helper_.setBufferLimits(kBufferLimit, kBufferLimit);
    auto* processing_mode = proto_config_.mutable_processing_mode();
    processing_mode->set_request_header_mode(ProcessingMode::SKIP);
    processing_mode->set_request_body_mode(ProcessingMode::STREAMED);
    processing_mode->set_response_header_mode(ProcessingMode::SKIP);
    initializeConfig();
    HttpIntegrationTest::initialize();

    IntegrationStreamDecoderPtr response = startRequestFromClientThatReadsLast();

    // Send the first request chunk to open the side stream and forward to upstream.
    sendClientRequestChunks(/*num_chunks=*/1, kChunkSize, /*end_stream=*/false);
    processRequestBodyMessage(*grpc_upstreams_[0], /*first_message=*/true, std::nullopt);
    waitForUpstreamRequestData(kChunkSize);

    // Upstream sends a large response while the client is not reading. With response_body_mode
    // NONE, downstream high watermark pauses upstream but must not pause the side stream.
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(kResponseBodySize, false);
    verifyUpstreamAndSidestreamFlowControlPaused(/*expect_sidestream_paused=*/false);

    // Client finishes uploading; the server answers each request body chunk.
    sendClientRequestChunks(kChunksAfterPause, kChunkSize, /*end_stream=*/true);
    bool saw_end_of_stream = false;
    while (!saw_end_of_stream) {
      processRequestBodyMessage(*grpc_upstreams_[0], /*first_message=*/false,
                                [&saw_end_of_stream](const HttpBody& body, BodyResponse&) {
                                  saw_end_of_stream = body.end_of_stream();
                                  return true;
                                });
    }

    waitForUpstreamRequestData(kRequestBodySize);
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    verifyUpstreamAndSidestreamFlowControlPaused(/*expect_sidestream_paused=*/false);

    // Client reads the response after finishing its upload.
    upstream_request_->encodeData(0, true);
    request_encoder_->getStream().readDisable(false);
    verifyDownstreamResponse(*response, 200);
    EXPECT_EQ(kResponseBodySize, response->body().size());
    EXPECT_EQ(0, counterValue(kMessageTimeouts));
  }

  void runFullDuplexWithPassThroughServer(bool flow_control_3_enabled) {
    constexpr uint64_t kChunkSize = 16 * 1024;
    constexpr int kRequestChunks = 32;
    constexpr int kChunksBeforeResponse = 2;
    constexpr int kMiddleChunks = kRequestChunks - 1 - kChunksBeforeResponse;
    constexpr uint64_t kRequestBodySize = kChunkSize * kRequestChunks;
    constexpr uint64_t kEarlyResponseBodySize = 128 * 1024;
    constexpr uint64_t kTailResponseBodySize = 16 * 1024;
    const std::string early_response_body(kEarlyResponseBodySize, 'r');
    const std::string tail_response_body(kTailResponseBodySize, 't');

    setSidestreamFlowControl(flow_control_3_enabled);
    initializeConfigDuplexStreamed(/*both_direction=*/true);

    IntegrationStreamDecoderPtr response = startRequestFromClientThatReadsLast();

    // Send headers and initial request chunks to upstream.
    sendClientRequestChunks(kChunksBeforeResponse, kChunkSize, /*end_stream=*/false);
    processRequestHeadersMessage(*grpc_upstreams_[0], /*first_message=*/true, std::nullopt);
    echoRequestBytes(kChunksBeforeResponse * kChunkSize);
    waitForUpstreamRequestData(kChunksBeforeResponse * kChunkSize);

    // Upstream sends an early response that fills the downstream buffer above high watermark.
    // Side stream must not be paused while request upload is still in progress.
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData("early_resp", false);
    processResponseHeadersMessage(*grpc_upstreams_[0], /*first_message=*/false, std::nullopt);
    processResponseBodyMessage(
        *grpc_upstreams_[0], /*first_message=*/false, [&](const HttpBody& body, BodyResponse&) {
          EXPECT_FALSE(body.end_of_stream());
          serverSendBodyRespDuplexStreamed(/*total_resp_body_msg=*/1, processor_stream_,
                                           /*end_of_stream=*/false, /*response=*/true,
                                           early_response_body);
          return false;
        });
    verifyUpstreamAndSidestreamFlowControlPaused(/*expect_sidestream_paused=*/false);

    // Client continues uploading before end_stream; side stream remains unpaused.
    sendClientRequestChunks(kMiddleChunks, kChunkSize, /*end_stream=*/false);
    echoRequestBytes(kMiddleChunks * kChunkSize);
    waitForUpstreamRequestData((kRequestChunks - 1) * kChunkSize);
    verifyUpstreamAndSidestreamFlowControlPaused(/*expect_sidestream_paused=*/false);

    // Client sends the final chunk (end_stream = true). With flow control 3 on,
    // the side stream now pauses.
    sendClientRequestChunks(/*num_chunks=*/1, kChunkSize, /*end_stream=*/true);
    echoRequestBytes(kChunkSize);
    verifyUpstreamAndSidestreamFlowControlPaused(
        /*expect_sidestream_paused=*/flow_control_3_enabled);

    // Client starts reading, draining the downstream buffer so both directions complete.
    request_encoder_->getStream().readDisable(false);
    waitForUpstreamRequestData(kRequestBodySize);
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    upstream_request_->encodeData(tail_response_body, true);
    (void)serverReceiveBodyDuplexStreamed(tail_response_body, processor_stream_,
                                          /*response=*/true, /*compare_body=*/true);
    serverSendBodyRespDuplexStreamed(/*total_resp_body_msg=*/1, processor_stream_,
                                     /*end_of_stream=*/true, /*response=*/true, tail_response_body);

    verifyDownstreamResponse(*response, 200);
    if (flow_control_3_enabled) {
      EXPECT_GE(counterValue(kSidestreamResumed), 1);
    }
    EXPECT_EQ(kEarlyResponseBodySize + kTailResponseBodySize, response->body().size());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ExtProcSidestreamPushbackDeadlockTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

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
