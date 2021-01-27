#include "envoy/http/header_map.h"

#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor;
using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::LowerCaseString;

using testing::Invoke;
using testing::Unused;

using namespace std::chrono_literals;

// These tests directly drive the filter. They concentrate on testing out all the different
// ordering options for the protocol, which means that unlike other tests they do not verify
// every parameter sent to or from the filter.

class OrderingTest : public testing::Test {
protected:
  void initialize(absl::optional<std::function<void(ExternalProcessor&)>> cb) {
    client_ = std::make_unique<MockClient>();
    EXPECT_CALL(*client_, start(_, _)).WillOnce(Invoke(this, &OrderingTest::doStart));

    ExternalProcessor proto_config;
    proto_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    if (cb) {
      (*cb)(proto_config);
    }
    config_.reset(new FilterConfig(proto_config, 200ms, stats_store_, ""));
    filter_ = std::make_unique<Filter>(config_, std::move(client_));
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void TearDown() override { filter_->onDestroy(); }

  // Called by the "start" method on the stream by the filter
  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks,
                                     const std::chrono::milliseconds&) {
    stream_callbacks_ = &callbacks;
    auto stream = std::make_unique<MockStream>();
    EXPECT_CALL(*stream, send(_, _)).WillRepeatedly(Invoke(this, &OrderingTest::doSend));
    EXPECT_CALL(*stream, close());
    return stream;
  }

  // Called on the stream after it's been created. These delegate
  // to "stream_delegate_" so that we can have expectations there.

  void doSend(ProcessingRequest&& request, bool end_stream) {
    stream_delegate_.send(std::move(request), end_stream);
  }

  // Send data through the filter as if we are the proxy

  void sendRequestHeadersGet(bool expect_callback) {
    HttpTestUtility::addDefaultHeaders(request_headers_, "GET");
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopAllIterationAndWatermark
                              : FilterHeadersStatus::Continue,
              filter_->decodeHeaders(request_headers_, true));
  }

  void sendRequestHeadersPost(bool expect_callback) {
    HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
    request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
    request_headers_.addCopy(LowerCaseString("content-length"), "10");
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopAllIterationAndWatermark
                              : FilterHeadersStatus::Continue,
              filter_->decodeHeaders(request_headers_, false));
  }

  void sendResponseHeaders(bool expect_callback) {
    response_headers_.setStatus(200);
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopAllIterationAndWatermark
                              : FilterHeadersStatus::Continue,
              filter_->encodeHeaders(response_headers_, false));
  }

  void sendRequestBody(FilterDataStatus expected_status, bool end_stream) {
    Buffer::OwnedImpl data;
    data.add("Dummy data");
    EXPECT_EQ(expected_status, filter_->decodeData(data, end_stream));
  }

  void sendResponseBody(FilterDataStatus expected_status, bool end_stream) {
    Buffer::OwnedImpl data;
    data.add("Dummy data");
    EXPECT_EQ(expected_status, filter_->encodeData(data, end_stream));
  }

  void sendRequestTrailers() {
    EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  }

  void sendResponseTrailers() {
    EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  }

  // Make it easier to send responses from the external processor

  void sendRequestHeadersReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_request_headers();
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendResponseHeadersReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_response_headers();
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendRequestBodyReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_request_body();
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendResponseBodyReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_response_body();
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendResponseTrailersReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_response_trailers();
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendImmediateResponse500() {
    auto reply = std::make_unique<ProcessingResponse>();
    auto* ir = reply->mutable_immediate_response();
    ir->mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendGrpcError() { stream_callbacks_->onGrpcError(Grpc::Status::Internal); }

  void closeGrpcStream() { stream_callbacks_->onGrpcClose(); }

  // Make it easier to handle buffering

  void expectBufferedRequest(Buffer::Instance& buf) {
    EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
        .WillOnce(Invoke([&buf](std::function<void(Buffer::Instance&)> f) { f(buf); }));
  }

  void expectBufferedResponse(Buffer::Instance& buf) {
    EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
        .WillOnce(Invoke([&buf](std::function<void(Buffer::Instance&)> f) { f(buf); }));
  }

  std::unique_ptr<MockClient> client_;
  MockStream stream_delegate_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<Filter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

// *** Tests for the normal processing path ***

// A normal call with the default configuration
TEST_F(OrderingTest, DefaultOrderingGet) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// A normal call with all supported callbacks turned on
TEST_F(OrderingTest, DefaultOrderingAllCallbacks) {
  Logger::Registry::getLog(Logger::Id::filter).set_level(spdlog::level::trace);
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body_1;
  req_body_1.add("Dummy data 1");
  Buffer::OwnedImpl req_body_2;
  req_body_2.add("Dummy data 2");
  Buffer::OwnedImpl req_buffer;
  req_buffer.add(req_body_1);
  req_buffer.add(req_body_2);

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, false));

  expectBufferedRequest(req_buffer);
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestBody(FilterDataStatus::StopIterationAndBuffer, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestBodyReply();

  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(nullptr));
  sendResponseBody(FilterDataStatus::StopIterationNoBuffer, true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
  sendResponseTrailers();
}

// A normal call with all supported callbacks turned on,
// but with request and response streams interleaved,
// as if the upstream ignores the request body and replies
// right away.
TEST_F(OrderingTest, DefaultOrderingAllCallbacksInterleaved) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  Buffer::OwnedImpl req_body_1;
  req_body_1.add("Dummy data 1");
  Buffer::OwnedImpl req_body_2;
  req_body_2.add("Dummy data 2");
  Buffer::OwnedImpl req_buffer;
  req_buffer.add(req_body_1);
  req_buffer.add(req_body_2);

  Buffer::OwnedImpl resp_body_1;
  resp_body_1.add("Dummy response data 1");
  Buffer::OwnedImpl resp_body_2;
  resp_body_2.add("Dummy response data 2");
  Buffer::OwnedImpl resp_buffer;
  resp_buffer.add(resp_body_1);
  resp_buffer.add(resp_body_2);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_1, false));

  EXPECT_CALL(stream_delegate_, send(_, false));
  expectBufferedRequest(req_buffer);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_2, true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestBodyReply();

  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  expectBufferedResponse(resp_buffer);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_2, true));
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
  sendResponseTrailers();
}

// An immediate response on the request path
TEST_F(OrderingTest, ImmediateResponseOnRequest) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendImmediateResponse500();
  // The rest of the filter isn't necessarily called after this.
}

// An immediate response on the response path
TEST_F(OrderingTest, ImmediateResponseOnResponse) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendImmediateResponse500();
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// *** Tests of out-of-order messages ***
// In general, for these the server closes the stream and ignores the
// processor for the rest of the filter lifetime.

// Receive a response headers reply in response to the request
// headers message -- should close stream and stop sending, but otherwise
// continue without error.
TEST_F(OrderingTest, IncorrectRequestHeadersReply) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendResponseHeadersReply(); // Wrong message here
  sendRequestTrailers();

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// Receive a response trailers reply in response to the request
// headers message -- should close stream and stop sending, but otherwise
// continue without error.
TEST_F(OrderingTest, IncorrectRequestHeadersReply2) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendResponseTrailersReply(); // Wrong message here
  sendRequestTrailers();

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// Receive a response body reply in response to the request
// body message -- should close stream and stop sending, but otherwise
// continue without error.
TEST_F(OrderingTest, IncorrectRequestBodyReply) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  sendRequestBody(FilterDataStatus::StopIterationNoBuffer, true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendResponseBodyReply(); // Wrong message here
  sendRequestTrailers();

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// Receive a request headers reply in response to the response
// headers message -- should continue without error.
TEST_F(OrderingTest, IncorrectResponseHeadersReply) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendRequestHeadersReply();
  // Still should ignore the message and go on but send no more stream messages
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// Receive an extra message -- we should ignore it
// and not send anything else to the server
TEST_F(OrderingTest, ExtraReply) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  // Extra call
  sendRequestHeadersReply();

  // After this we are ignoring the processor
  sendRequestTrailers();
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// Receive an extra message after the immediate response -- it should
// be ignored.
TEST_F(OrderingTest, ExtraAfterImmediateResponse) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendImmediateResponse500();
  // Extra messages sent after immediate response shouldn't affect anything
  sendRequestHeadersReply();
}

// *** Tests of gRPC stream state ***

// gRPC error in response to message calls results in an error
TEST_F(OrderingTest, GrpcErrorInline) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendGrpcError();
  // The rest of the filter isn't called after this.
}

// gRPC error in response to message results in connection being dropped
// if failures are ignored
TEST_F(OrderingTest, GrpcErrorInlineIgnored) {
  initialize([](ExternalProcessor& cfg) { cfg.set_failure_mode_allow(true); });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendGrpcError();

  // After that we ignore the processor
  sendRequestTrailers();
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

// gRPC error in between calls should still be delivered
TEST_F(OrderingTest, GrpcErrorOutOfLine) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendGrpcError();
}

// gRPC close after a proper message means rest of stream is ignored
TEST_F(OrderingTest, GrpcCloseAfter) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  closeGrpcStream();

  // After that we ignore the processor
  sendRequestTrailers();
  sendResponseHeaders(false);
  sendResponseBody(FilterDataStatus::Continue, true);
  sendResponseTrailers();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
