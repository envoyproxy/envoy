#include <algorithm>
#include <iostream>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/network/address.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/json/json_loader.h"
#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/logging_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.h"
#include "test/extensions/filters/http/ext_proc/tracer_test_filter.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using Envoy::Extensions::HttpFilters::ExternalProcessing::verifyMultipleHeaderValues;
using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::HttpTrailers;
using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::ProtocolConfiguration;
using envoy::service::ext_proc::v3::TrailersResponse;
using Extensions::HttpFilters::ExternalProcessing::DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS;
using Extensions::HttpFilters::ExternalProcessing::HeaderProtosEqual;
using Extensions::HttpFilters::ExternalProcessing::makeHeaderValue;
using Extensions::HttpFilters::ExternalProcessing::OnProcessingResponseFactory;
using Extensions::HttpFilters::ExternalProcessing::TestOnProcessingResponseFactory;
using Http::LowerCaseString;
using test::integration::filters::LoggingTestFilterConfig;
using testing::_;
using testing::Not;

using namespace std::chrono_literals;

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing, ExtProcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ExtProcIntegrationTest, ServerWaitForBodyBeforeSendsHeaderRespDuplexStreamed) {
  const std::string body_sent(64 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, true);

  // The ext_proc server receives the headers.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  // The ext_proc server receives the body.
  uint32_t total_req_body_msg = serverReceiveBodyDuplexStreamed(body_sent);

  // The ext_proc server sends back the header response.
  serverSendHeaderRespDuplexStreamed();
  // The ext_proc server sends back the body response.
  uint32_t total_resp_body_msg = 2 * total_req_body_msg;
  const std::string body_upstream(total_resp_body_msg, 'r');
  serverSendBodyRespDuplexStreamed(total_resp_body_msg);

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, LargeBodyTestDuplexStreamed) {
  const std::string body_sent(2 * 1024 * 1024, 's');
  initializeConfigDuplexStreamed(false);

  // Sends 30 consecutive request, each carrying 2MB data.
  for (int i = 0; i < 30; i++) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl default_headers;
    HttpTestUtility::addDefaultHeaders(default_headers);

    std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> encoder_decoder =
        codec_client_->startRequest(default_headers);
    request_encoder_ = &encoder_decoder.first;
    IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, body_sent, true);
    // The ext_proc server receives the headers.
    ProcessingRequest header_request;
    serverReceiveHeaderDuplexStreamed(header_request);
    // The ext_proc server receives the body.
    uint32_t total_req_body_msg = serverReceiveBodyDuplexStreamed(body_sent);
    EXPECT_GT(total_req_body_msg, 0);
    // The ext_proc server sends back the header response.
    serverSendHeaderRespDuplexStreamed();
    // The ext_proc server sends back body responses, which include 50 chunks,
    // and each chunk contains 64KB data, thus totally ~3MB per request.
    uint32_t total_resp_body_msg = 50;
    const std::string body_response(64 * 1024, 'r');
    const std::string body_upstream(total_resp_body_msg * 64 * 1024, 'r');
    serverSendBodyRespDuplexStreamed(total_resp_body_msg, /*end_of_stream*/ true,
                                     /*response*/ false, body_response);

    handleUpstreamRequest();
    EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
    EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
    verifyDownstreamResponse(*response, 200);
    TearDown();
  }
}

// Buffer the whole message including header, body and trailer before sending response.
TEST_P(ExtProcIntegrationTest,
       ServerWaitForBodyAndTrailerBeforeSendsHeaderRespDuplexStreamedSmallBody) {
  const std::string body_sent(128 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  // The ext_proc server receives the headers.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);

  std::string body_received;
  bool end_stream = false;
  uint32_t total_req_body_msg = 0;
  while (!end_stream) {
    ProcessingRequest request;
    EXPECT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_TRUE(request.has_request_body() || request.has_request_trailers());
    if (!request.has_request_trailers()) {
      // request_body is received
      body_received = absl::StrCat(body_received, request.request_body().body());
      total_req_body_msg++;
    } else {
      // request_trailer is received.
      end_stream = true;
    }
  }
  EXPECT_TRUE(end_stream);
  EXPECT_EQ(body_received, body_sent);

  // The ext_proc server sends back the header response.
  serverSendHeaderRespDuplexStreamed();

  // The ext_proc server sends back the body response.
  uint32_t total_resp_body_msg = total_req_body_msg / 2;
  const std::string body_upstream(total_resp_body_msg, 'r');
  serverSendBodyRespDuplexStreamed(total_resp_body_msg, false);

  // The ext_proc server sends back the trailer response.
  serverSendTrailerRespDuplexStreamed();

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
  verifyDownstreamResponse(*response, 200);
}

// The body is large. The server sends some body responses after buffering some amount of data.
// The server continuously does so until the entire body processing is done.
TEST_P(ExtProcIntegrationTest, ServerSendBodyRespWithouRecvEntireBodyDuplexStreamed) {
  const std::string body_sent(256 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  // The ext_proc server receives the headers.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  Http::TestRequestHeaderMapImpl expected_request_headers{{":scheme", "http"},
                                                          {":method", "GET"},
                                                          {"host", "host"},
                                                          {":path", "/"},
                                                          {"x-forwarded-proto", "http"}};
  EXPECT_THAT(header_request.request_headers().headers(),
              HeaderProtosEqual(expected_request_headers));

  std::string body_received;
  bool end_stream = false;
  uint32_t total_req_body_msg = 0;
  bool header_resp_sent = false;
  std::string body_upstream;

  while (!end_stream) {
    ProcessingRequest request;
    EXPECT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
    EXPECT_TRUE(request.has_request_body() || request.has_request_trailers());
    if (!request.has_request_trailers()) {
      // Buffer the entire body.
      body_received = absl::StrCat(body_received, request.request_body().body());
      total_req_body_msg++;
      // After receiving every 7 body chunks, the server sends back three body responses.
      if (total_req_body_msg % 7 == 0) {
        if (!header_resp_sent) {
          // Before sending the 1st body response, sends a header response.
          serverSendHeaderRespDuplexStreamed();
          header_resp_sent = true;
        }
        ProcessingResponse response_body;
        for (uint32_t i = 0; i < 3; i++) {
          body_upstream += std::to_string(i);
          auto* streamed_response = response_body.mutable_request_body()
                                        ->mutable_response()
                                        ->mutable_body_mutation()
                                        ->mutable_streamed_response();
          streamed_response->set_body(std::to_string(i));
          processor_stream_->sendGrpcMessage(response_body);
        }
      }
    } else {
      // request_trailer is received.
      end_stream = true;
      Http::TestResponseTrailerMapImpl expected_trailers{{"x-trailer-foo", "yes"}};
      EXPECT_THAT(request.request_trailers().trailers(), HeaderProtosEqual(expected_trailers));
    }
  }
  EXPECT_TRUE(end_stream);
  EXPECT_EQ(body_received, body_sent);

  // Send one more body response at the end.
  ProcessingResponse response_body;
  auto* streamed_response = response_body.mutable_request_body()
                                ->mutable_response()
                                ->mutable_body_mutation()
                                ->mutable_streamed_response();
  streamed_response->set_body("END");
  processor_stream_->sendGrpcMessage(response_body);
  body_upstream += "END";

  // The ext_proc server sends back the trailer response.
  serverSendTrailerRespDuplexStreamed();

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, DuplexStreamedInBothDirection) {
  const std::string body_sent(8 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, true, true);

  // The ext_proc server receives the headers/body.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  uint32_t total_req_body_msg = serverReceiveBodyDuplexStreamed(body_sent);

  // The ext_proc server sends back the response.
  serverSendHeaderRespDuplexStreamed();
  uint32_t total_resp_body_msg = 2 * total_req_body_msg;
  const std::string body_upstream(total_resp_body_msg, 'r');
  serverSendBodyRespDuplexStreamed(total_resp_body_msg);

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);

  // The ext_proc server receives the responses from backend server.
  ProcessingRequest header_response;
  serverReceiveHeaderDuplexStreamed(header_response, false, true);
  uint32_t total_rsp_body_msg = serverReceiveBodyDuplexStreamed("", true, false);

  // The ext_proc server sends back the response.
  serverSendHeaderRespDuplexStreamed(false, true);
  serverSendBodyRespDuplexStreamed(total_rsp_body_msg * 3, true, true);

  verifyDownstreamResponse(*response, 200);
}

// With FULL_DUPLEX_STREAMED mode configured, failure_mode_allow can only be false.
// If the ext_proc server sends out-of-order response, it causes Envoy to send
// local reply to the client, and reset the HTTP stream.
TEST_P(ExtProcIntegrationTest, ServerSendOutOfOrderResponseDuplexStreamed) {
  const std::string body_sent(8 * 1024, 's');
  // Enable FULL_DUPLEX_STREAMED body processing in both directions.
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, true, true);

  // The ext_proc server receives the request headers and body.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  uint32_t total_req_body_msg = serverReceiveBodyDuplexStreamed(body_sent);
  // The ext_proc server sends back the body response, which is wrong.
  processor_stream_->startGrpcStream();
  serverSendBodyRespDuplexStreamed(total_req_body_msg);
  // Envoy sends 500 response code to the client.
  verifyDownstreamResponse(*response, 500);
}

// The ext_proc server failed to send response in time trigger Envoy HCM stream_idle_timeout.
TEST_P(ExtProcIntegrationTest, ServerWaitTooLongBeforeSendRespDuplexStreamed) {
  // Set HCM stream_idle_timeout to be 10s. Note one can also set the
  // RouteAction:idle_timeout under the route configuration to override it.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_stream_idle_timeout()->set_seconds(10); });

  const std::string body_sent(8 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, true);

  // The ext_proc server receives the headers and body.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  serverReceiveBodyDuplexStreamed(body_sent);

  // The ext_proc server waits for 12s before sending any response.
  // HCM stream_idle_timeout is triggered, and local reply is sent to downstream.
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(12000));
  verifyDownstreamResponse(*response, 504);
}

// Testing the case that when the client does not send trailers, if the ext_proc server sends
// back a synthesized trailer, it is ignored by Envoy and never reaches the upstream server.
TEST_P(ExtProcIntegrationTest, DuplexStreamedServerResponseWithSynthesizedTrailer) {
  const std::string body_sent(64 * 1024, 's');
  IntegrationStreamDecoderPtr response = initAndSendDataDuplexStreamedMode(body_sent, true);

  // The ext_proc server receives the headers.
  ProcessingRequest header_request;
  serverReceiveHeaderDuplexStreamed(header_request);
  // The ext_proc server receives the body.
  uint32_t total_req_body_msg = serverReceiveBodyDuplexStreamed(body_sent);

  // The ext_proc server sends back the header response.
  serverSendHeaderRespDuplexStreamed();
  // The ext_proc server sends back the body response.
  uint32_t total_resp_body_msg = 2 * total_req_body_msg;
  const std::string body_upstream(total_resp_body_msg, 'r');
  // The end_of_stream of the last body response is false.
  serverSendBodyRespDuplexStreamed(total_resp_body_msg, false, false);
  // The ext_proc server sends back a synthesized trailer response.
  serverSendTrailerRespDuplexStreamed();

  handleUpstreamRequest();
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-new-header", "new"));
  EXPECT_EQ(upstream_request_->body().toString(), body_upstream);
  EXPECT_EQ(upstream_request_->trailers(), nullptr);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ModeOverrideNoneToFullDuplex) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_allow_mode_override(true);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = std::string(10, 'a');
  std::string upstream_body_str = std::string(5, 'b');
  auto response = sendDownstreamRequestWithBody(body_str, absl::nullopt);
  // Process request header message.
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest&, ProcessingResponse& resp) {
        resp.mutable_request_headers();
        resp.mutable_mode_override()->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false,
      [&body_str, &upstream_body_str](const HttpBody& body, BodyResponse& resp) {
        EXPECT_TRUE(body.end_of_stream());
        EXPECT_EQ(body.body(), body_str);
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_body(upstream_body_str);
        streamed_response->set_end_of_stream(true);
        return true;
      });
  handleUpstreamRequest();
  EXPECT_EQ(upstream_request_->body().toString(), upstream_body_str);
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, NoneToFullDuplexMoreDataAfterModeOverride) {
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_allow_mode_override(true);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  // Process request header message.
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest&, ProcessingResponse& resp) {
        resp.mutable_request_headers();
        resp.mutable_mode_override()->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& resp) {
        EXPECT_FALSE(body.end_of_stream());
        EXPECT_EQ(body.body().size(), 10);
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_body("bbbbb");
        streamed_response->set_end_of_stream(false);
        return true;
      });

  codec_client_->sendData(*request_encoder_, 20, true);

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& resp) {
        EXPECT_TRUE(body.end_of_stream());
        EXPECT_EQ(body.body().size(), 20);
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_body("0123456789");
        streamed_response->set_end_of_stream(true);
        return true;
      });

  handleUpstreamRequest();
  EXPECT_EQ(upstream_request_->body().toString(), "bbbbb0123456789");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcIntegrationTest, ServerWaitforEnvoyHalfCloseThenCloseStream) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.ext_proc_graceful_grpc_close", "true"}});
  proto_config_.mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::FULL_DUPLEX_STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("foo", absl::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [](const HttpHeaders& headers, HeadersResponse&) {
                                 EXPECT_FALSE(headers.end_of_stream());
                                 return true;
                               });
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& resp) {
        EXPECT_TRUE(body.end_of_stream());
        EXPECT_EQ(body.body().size(), 3);
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_body("bar");
        streamed_response->set_end_of_stream(true);
        return true;
      });

  // Server closes the stream.
  processor_stream_->finishGrpcStream(Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
