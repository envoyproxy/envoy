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
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;

using testing::Invoke;
using testing::Unused;

using namespace std::chrono_literals;

// These tests directly drive the filter. They concentrate on testing out all the different
// ordering options for the protocol, which means that unlike other tests they do not verify
// every parameter sent to or from the filter.

class OrderingTest : public testing::Test {
protected:
  void initialize(std::function<void(ExternalProcessor&)> cb) {
    client_ = std::make_unique<MockClient>();
    EXPECT_CALL(*client_, start(_, _)).WillOnce(Invoke(this, &OrderingTest::doStart));

    ExternalProcessor proto_config;
    proto_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    if (cb != nullptr) {
      cb(proto_config);
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
    EXPECT_CALL(*stream, close()).WillRepeatedly(Invoke(this, &OrderingTest::doSendClose));
    return stream;
  }

  // Called on the stream after it's been created. These delegate
  // to "stream_delegate_" so that we can have expectations there.

  void doSend(ProcessingRequest&& request, bool end_stream) {
    stream_delegate_.send(std::move(request), end_stream);
  }

  void doSendClose() { stream_delegate_.close(); }

  // Send data through the filter as if we are the proxy

  void sendRequestHeadersGet(bool expect_callback) {
    HttpTestUtility::addDefaultHeaders(request_headers_, "GET");
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopAllIterationAndWatermark
                              : FilterHeadersStatus::Continue,
              filter_->decodeHeaders(request_headers_, true));
  }

  void sendResponseHeaders(bool expect_callback) {
    response_headers_.setStatus(200);
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopAllIterationAndWatermark
                              : FilterHeadersStatus::Continue,
              filter_->encodeHeaders(response_headers_, false));
  }

  void sendRequestBody() {
    Buffer::OwnedImpl data;
    data.add("Dummy data");
    EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data, true));
  }

  void sendResponseBody() {
    Buffer::OwnedImpl data;
    data.add("Dummy data");
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data, true));
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

  void sendImmediateResponse500() {
    auto reply = std::make_unique<ProcessingResponse>();
    auto* ir = reply->mutable_immediate_response();
    ir->mutable_status()->set_code(envoy::type::v3::StatusCode::InternalServerError);
    stream_callbacks_->onReceiveMessage(std::move(reply));
  }

  void sendGrpcError() { stream_callbacks_->onGrpcError(Grpc::Status::Internal); }

  void closeGrpcStream() { stream_callbacks_->onGrpcClose(); }

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
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();
  sendResponseBody();
  sendResponseTrailers();

  EXPECT_CALL(stream_delegate_, close());
}

// An immediate response on the request path
TEST_F(OrderingTest, ImmediateResponseOnRequest) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  // sendLocalReply will call this
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
  EXPECT_CALL(stream_delegate_, close());
  sendImmediateResponse500();
  // The rest of the filter isn't necessarily called after this.
}

// An immediate response on the response path
TEST_F(OrderingTest, ImmediateResponseOnResponse) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(stream_delegate_, close());
  sendImmediateResponse500();
  sendResponseBody();
  sendResponseTrailers();
}

// *** Tests of out-of-order messages ***
// In general, for these the server closes the stream and ignores the
// processor for the rest of the filter lifetime.

// Receive a response headers reply in response to the request
// headers message -- should close stream and stop sending, but otherwise
// continue without error.
TEST_F(OrderingTest, IncorrectRequestHeadersReply) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(stream_delegate_, close());
  sendResponseHeadersReply();
  sendRequestTrailers();

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  sendResponseBody();
  sendResponseTrailers();
}

// Receive a request headers reply in response to the response
// headers message -- should continue without error.
TEST_F(OrderingTest, IncorrectResponseHeadersReply) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  EXPECT_CALL(stream_delegate_, close());
  sendRequestHeadersReply();
  // Still should ignore the message and go on but send no more stream messages
  sendResponseBody();
  sendResponseTrailers();
}

// Receive an extra message -- we should ignore it
// and not send anything else to the server
TEST_F(OrderingTest, ExtraReply) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  // Extra call
  EXPECT_CALL(stream_delegate_, close());
  sendRequestHeadersReply();

  // After this we are ignoring the processor
  sendRequestTrailers();
  sendResponseHeaders(false);
  sendResponseBody();
  sendResponseTrailers();
}

// Receive an extra message after the immediate response -- it should
// be ignored.
TEST_F(OrderingTest, ExtraAfterImmediateResponse) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  // sendLocalReply invokes encoding too
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
  EXPECT_CALL(stream_delegate_, close());
  sendImmediateResponse500();
  // Extra messages sent after immediate response shouldn't affect anything
  sendRequestHeadersReply();
}

// *** Tests of gRPC stream state ***

// gRPC error in response to message calls results in an error
TEST_F(OrderingTest, GrpcErrorInline) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  // sendLocalReply will call this
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
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
  sendResponseBody();
  sendResponseTrailers();
}

// gRPC error in between calls should still be delivered
TEST_F(OrderingTest, GrpcErrorOutOfLine) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  sendRequestTrailers();

  sendGrpcError();
  // After error we should send no more messages,
  // but should return an error to the caller
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  sendResponseHeaders(false);
}

// gRPC close after a proper message means rest of stream is ignored
TEST_F(OrderingTest, GrpcCloseAfter) {
  initialize(nullptr);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();
  closeGrpcStream();

  // After that we ignore the processor
  sendRequestTrailers();
  sendResponseHeaders(false);
  sendResponseBody();
  sendResponseTrailers();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
