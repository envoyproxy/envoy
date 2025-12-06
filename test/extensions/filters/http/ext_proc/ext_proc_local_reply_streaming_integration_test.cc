#include <utility>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"
#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;
using Http::LowerCaseString;

class ExtProcLocalReplyStreamingIntegrationTest : public ExtProcIntegrationTest {
public:
  void sendLocalResponseBody(bool end_of_stream) {
    ProcessingResponse body_response;
    auto streamed_response =
        body_response.mutable_streamed_immediate_response()->mutable_body_response();
    streamed_response->set_body("local response body");
    streamed_response->set_end_of_stream(end_of_stream);
    processor_stream_->sendGrpcMessage(body_response);
  }

  void sendLocalResponseTrailers() {
    ProcessingResponse trailers_response;
    auto streamed_trailers = trailers_response.mutable_streamed_immediate_response()
                                 ->mutable_trailers_response()
                                 ->add_headers();
    streamed_trailers->set_key("some-trailer");
    streamed_trailers->set_raw_value("foobar");
    processor_stream_->sendGrpcMessage(trailers_response);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing,
                         ExtProcLocalReplyStreamingIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

void makeLocalResponseHeaders(ProcessingResponse& resp, bool end_of_stream) {
  auto local_response_headers =
      resp.mutable_streamed_immediate_response()->mutable_headers_response();
  local_response_headers->set_end_of_stream(end_of_stream);
  auto header = local_response_headers->mutable_headers()->add_headers();
  header->set_key(":status");
  header->set_raw_value("200");
  header = local_response_headers->mutable_headers()->add_headers();
  header->set_key("some-header");
  header->set_raw_value("foobar");
}

void makeLocalResponseBody(ProcessingResponse& resp, absl::string_view body, bool end_of_stream) {
  auto* streamed_response = resp.mutable_streamed_immediate_response()->mutable_body_response();
  streamed_response->set_body(body);
  streamed_response->set_end_of_stream(end_of_stream);
}

void makeLocalResponseTrailers(ProcessingResponse& resp) {
  auto* add =
      resp.mutable_streamed_immediate_response()->mutable_trailers_response()->add_headers();
  add->set_key("x-some-other-trailer");
  add->set_raw_value("no");
}

void expectRequestBody(const ProcessingRequest& req, absl::string_view body, bool end_of_stream) {
  EXPECT_TRUE(req.has_request_body());
  EXPECT_EQ(req.request_body().body(), body);
  EXPECT_EQ(req.request_body().end_of_stream(), end_of_stream);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, LocalHeadersOnlyRequestAndResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, true);
                          return true;
                        });

  ASSERT_TRUE(response->waitForEndStream());
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, LocalHeadersOnlyRequestAndResponseWithBody) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  sendLocalResponseBody(true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "local response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest,
       LocalHeadersOnlyRequestAndResponseWithBodyAndTrailers) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  sendLocalResponseBody(false);
  sendLocalResponseTrailers();

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "local response body");
  EXPECT_THAT(*(response->trailers()), ContainsHeader("some-trailer", "foobar"));
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, RequestWithBodySkipAndLocalHeadersOnlyResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequestWithBody("request body", [](Http::HeaderMap& headers) {
    headers.addCopy(LowerCaseString("x-remove-this"), "yes");
  });

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, true);
                          return true;
                        });

  ASSERT_TRUE(response->waitForEndStream());
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, RequestWithBodySkipAndLocalResponseWithBody) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  codec_client_->sendData(*request_encoder_, 10, true);

  sendLocalResponseBody(true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "local response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest,
       RequestWithBodyAndTrailersSkipAndLocalResponseWithBody) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  codec_client_->sendData(*request_encoder_, 10, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  sendLocalResponseBody(true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "local response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest,
       FinishLocalResponseWithIncomplereRequestWithBody) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  // Leave the request body incomplete.
  codec_client_->sendData(*request_encoder_, 10, false);

  sendLocalResponseBody(true);

  ASSERT_TRUE(response->waitForReset());
  ASSERT_EQ(response->body(), "local response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest,
       FinishLocalResponseWithIncomplereRequestWithBodyDuplex) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::FULL_DUPLEX_STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  // Leave the request body incomplete.
  codec_client_->sendData(*request_encoder_, 10, false);

  sendLocalResponseBody(true);

  ASSERT_TRUE(response->waitForReset());
  ASSERT_EQ(response->body(), "local response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, RequestWithBodyDuplexAndLocalResponseWithBody) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::FULL_DUPLEX_STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  codec_client_->sendData(*request_encoder_, 5, true);

  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest& req, ProcessingResponse& resp) {
                          expectRequestBody(req, "aaaaaaaaaa", false);
                          makeLocalResponseBody(resp, "bbbb", false);
                          return true;
                        });

  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest& req, ProcessingResponse& resp) {
                          expectRequestBody(req, "aaaaa", true);
                          makeLocalResponseBody(resp, "cccccccc", true);
                          return true;
                        });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "bbbbcccccccc");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest,
       RequestWithBodyAndTrailersDuplexAndLocalResponseWithBodyAndTrailers) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::FULL_DUPLEX_STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  codec_client_->sendData(*request_encoder_, 10, false);

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();
  codec_client_->sendData(*request_encoder_, 5, false);

  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest& req, ProcessingResponse& resp) {
                          expectRequestBody(req, "aaaaaaaaaa", false);
                          makeLocalResponseBody(resp, "bbbb", false);
                          return true;
                        });

  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest& req, ProcessingResponse& resp) {
                          expectRequestBody(req, "aaaaa", false);
                          makeLocalResponseBody(resp, "cccccccc", false);
                          return true;
                        });

  sendLocalResponseBody(false);
  codec_client_->sendTrailers(*request_encoder_,
                              Http::TestRequestTrailerMapImpl{{"x-trailer-foo", "yes"}});
  processGenericMessage(*grpc_upstreams_[0], false,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseTrailers(resp);
                          return true;
                        });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->body(), "bbbbcccccccclocal response body");
  verifyDownstreamResponse(*response, 200);
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, SpuriousResponse) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::FULL_DUPLEX_STREAMED);
  proto_config_.mutable_processing_mode()->set_request_trailer_mode(ProcessingMode::SEND);
  initializeConfig();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  response->waitForHeaders();

  ProcessingResponse body_response;
  body_response.mutable_request_body()->mutable_response()->mutable_body_mutation()->set_body(
      "foobar");
  processor_stream_->sendGrpcMessage(body_response);

  // Because the local response headers have already been sent the response will be reset.
  // Client will still observe the 200 in the response headers.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

TEST_P(ExtProcLocalReplyStreamingIntegrationTest, TimeoutWaitingForLocalResponseEndStream) {
  // Set HCM stream_idle_timeout to be 10s. Note one can also set the
  // RouteAction:idle_timeout under the route configuration to override it.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_stream_idle_timeout()->set_seconds(10); });

  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(
      [](Http::HeaderMap& headers) { headers.addCopy(LowerCaseString("x-remove-this"), "yes"); });

  processGenericMessage(*grpc_upstreams_[0], true,
                        [](const ProcessingRequest&, ProcessingResponse& resp) {
                          makeLocalResponseHeaders(resp, false);
                          return true;
                        });

  // Do not send local response body with end_of_stream=true.

  // HCM stream_idle_timeout is triggered, and downstream stream is reset, because
  // headers were already sent.
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(12000));

  ASSERT_TRUE(response->waitForReset());
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
