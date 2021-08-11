#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/mocks/event/mocks.h"
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

using Event::MockTimer;
using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::LowerCaseString;

using testing::AnyNumber;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::Unused;

using namespace std::chrono_literals;

// The value to return for the decoder buffer limit.
static const uint32_t BufferSize = 100000;

// These tests directly drive the filter. They concentrate on testing out all the different
// ordering options for the protocol, which means that unlike other tests they do not verify
// every parameter sent to or from the filter.

class OrderingTest : public testing::Test {
protected:
  static constexpr std::chrono::milliseconds kMessageTimeout = 200ms;
  static constexpr auto kMessageTimeoutNanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(kMessageTimeout).count();

  void initialize(absl::optional<std::function<void(ExternalProcessor&)>> cb) {
    client_ = std::make_unique<MockClient>();
    EXPECT_CALL(*client_, start(_)).WillOnce(Invoke(this, &OrderingTest::doStart));
    EXPECT_CALL(encoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));

    ExternalProcessor proto_config;
    proto_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    if (cb) {
      (*cb)(proto_config);
    }
    config_.reset(new FilterConfig(proto_config, kMessageTimeout, stats_store_, ""));
    filter_ = std::make_unique<Filter>(config_, std::move(client_));
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void TearDown() override { filter_->onDestroy(); }

  // Called by the "start" method on the stream by the filter
  virtual ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks) {
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
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopIteration : FilterHeadersStatus::Continue,
              filter_->decodeHeaders(request_headers_, true));
  }

  void sendRequestHeadersPost(bool expect_callback) {
    HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
    request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
    request_headers_.addCopy(LowerCaseString("content-length"), "10");
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopIteration : FilterHeadersStatus::Continue,
              filter_->decodeHeaders(request_headers_, false));
  }

  void sendResponseHeaders(bool expect_callback) {
    response_headers_.setStatus(200);
    EXPECT_EQ(expect_callback ? FilterHeadersStatus::StopIteration : FilterHeadersStatus::Continue,
              filter_->encodeHeaders(response_headers_, false));
  }

  void sendRequestTrailers(bool expect_callback) {
    EXPECT_EQ(expect_callback ? FilterTrailersStatus::StopIteration
                              : FilterTrailersStatus::Continue,
              filter_->decodeTrailers(request_trailers_));
  }

  void sendResponseTrailers(bool expect_callback) {
    EXPECT_EQ(expect_callback ? FilterTrailersStatus::StopIteration
                              : FilterTrailersStatus::Continue,
              filter_->encodeTrailers(response_trailers_));
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

  void sendRequestTrailersReply() {
    auto reply = std::make_unique<ProcessingResponse>();
    reply->mutable_request_trailers();
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

  void expectBufferedRequest(Buffer::Instance& buf, bool expect_modification) {
    EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
        .WillRepeatedly(
            Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
    if (expect_modification) {
      EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
          .WillOnce(Invoke([&buf](std::function<void(Buffer::Instance&)> f) { f(buf); }));
    }
  }

  void expectBufferedResponse(Buffer::Instance& buf, bool expect_modification) {
    EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
        .WillRepeatedly(
            Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
    if (expect_modification) {
      EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
          .WillOnce(Invoke([&buf](std::function<void(Buffer::Instance&)> f) { f(buf); }));
    }
  }

  std::unique_ptr<MockClient> client_;
  MockStream stream_delegate_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

// A base class for tests that will check that gRPC streams fail while being created

class FastFailOrderingTest : public OrderingTest {
  // All tests using this class have gRPC streams that will fail while being opened.
  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks) override {
    auto stream = std::make_unique<MockStream>();
    EXPECT_CALL(*stream, close());
    callbacks.onGrpcError(Grpc::Status::Internal);
    return stream;
  }
};

// *** Tests for the normal processing path ***

// A call with a totally crazy response
TEST_F(OrderingTest, TotallyInvalidResponse) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  auto reply = std::make_unique<ProcessingResponse>();
  // Totally empty response is spurious -- we should ignore the server for
  // all subsequent callbacks as we do for other spurious messages.
  stream_callbacks_->onReceiveMessage(std::move(reply));

  sendResponseHeaders(false);
  Buffer::OwnedImpl req_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(req_body, true));
}

// A normal call with the default configuration
TEST_F(OrderingTest, DefaultOrderingGet) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();
  Buffer::OwnedImpl req_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(req_body, true));
}

// A normal call with the default configuration, with a mock timer so that we can
// verify timer behavior.
TEST_F(OrderingTest, DefaultOrderingGetWithTimer) {
  initialize([](ExternalProcessor& proto_config) {
    proto_config.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
  });

  // MockTimer constructor sets up expectations in the Dispatcher class to wire it up
  MockTimer* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(*request_timer, disableTimer());
  sendRequestHeadersReply();

  MockTimer* response_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*response_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  EXPECT_CALL(*response_timer, disableTimer());
  sendResponseHeadersReply();
  Buffer::OwnedImpl req_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(req_body, true));
}

// A normal call with all supported callbacks turned on
TEST_F(OrderingTest, DefaultOrderingHeadersBody) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body_1;
  req_body_1.add("Dummy data 1");
  Buffer::OwnedImpl req_body_2;
  req_body_2.add("Dummy data 2");
  Buffer::OwnedImpl req_buffer;
  expectBufferedRequest(req_buffer, true);

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, false));
  req_buffer.add(req_body_1);

  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body_2, true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestBodyReply();

  Buffer::OwnedImpl resp_body_1("Dummy response");
  Buffer::OwnedImpl resp_buffer;
  expectBufferedResponse(resp_buffer, true);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  sendResponseHeadersReply();
  EXPECT_CALL(stream_delegate_, send(_, false));
  // Remember that end_stream is false if there will be headers
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_1, false));
  resp_buffer.add(resp_body_1);
  sendResponseTrailers(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
}

// A normal call with all supported callbacks turned on
TEST_F(OrderingTest, DefaultOrderingEverything) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
    pm->set_request_trailer_mode(ProcessingMode::SEND);
    pm->set_response_trailer_mode(ProcessingMode::SEND);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body_1;
  req_body_1.add("Dummy data 1");
  Buffer::OwnedImpl req_body_2;
  req_body_2.add("Dummy data 2");
  Buffer::OwnedImpl req_buffer;
  expectBufferedRequest(req_buffer, true);

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, false));
  req_buffer.add(req_body_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_2, false));
  req_buffer.add(req_body_2);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestTrailers(true);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestBodyReply();
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestTrailersReply();

  Buffer::OwnedImpl resp_body_1("Dummy response");
  Buffer::OwnedImpl resp_buffer;
  expectBufferedResponse(resp_buffer, true);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  sendResponseHeadersReply();
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_1, false));
  resp_buffer.add(resp_body_1);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseTrailers(true);
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseBodyReply();
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseTrailersReply();
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
  sendRequestHeadersReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  sendResponseHeadersReply();

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_1, false));

  EXPECT_CALL(stream_delegate_, send(_, false));
  expectBufferedRequest(req_buffer, true);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body_2, true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestBodyReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  expectBufferedResponse(resp_buffer, true);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_body_2, true));
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
}

// A normal call with response buffering on. All response data comes back before the
// request callback finishes.
TEST_F(OrderingTest, ResponseAllDataComesFast) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  Buffer::OwnedImpl resp_body_1("Dummy response");
  Buffer::OwnedImpl resp_buffer;
  expectBufferedResponse(resp_buffer, true);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  // The rest of the data might come in even before the response headers
  // response comes back.
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_body_1, true));

  // When the response does comes back, we should immediately send the body to the server
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeadersReply();
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
}

// A normal call with response buffering on. Some response data comes back before the
// response headers callback finishes.
TEST_F(OrderingTest, ResponseSomeDataComesFast) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  Buffer::OwnedImpl resp_body_1("Dummy response");
  Buffer::OwnedImpl resp_body_2(" the end");
  Buffer::OwnedImpl resp_buffer;
  expectBufferedResponse(resp_buffer, true);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  // Some of the data might come back but we should watermark so that we
  // don't fill the buffer.
  EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->encodeData(resp_body_1, false));

  // When the response does comes back, we should lift the watermark
  EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  sendResponseHeadersReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_body_2, true));
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseBodyReply();
}

// Add trailers to the request path
TEST_F(OrderingTest, AddRequestTrailers) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_trailer_mode(ProcessingMode::SEND);
  });

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body_1;
  req_body_1.add("Dummy data 1");
  Buffer::OwnedImpl req_buffer;
  expectBufferedRequest(req_buffer, false);

  Http::TestRequestTrailerMapImpl response_trailers;

  // Expect the trailers callback to be sent in line with decodeData
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).WillOnce(ReturnRef(response_trailers));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_body_1, true));
  req_buffer.add(req_body_1);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestTrailersReply();

  Buffer::OwnedImpl resp_body_1("Dummy response");
  Buffer::OwnedImpl resp_buffer;
  expectBufferedResponse(resp_buffer, false);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendResponseHeadersReply();
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body_1, true));
}

// An immediate response on the request path
TEST_F(OrderingTest, ImmediateResponseOnRequest) {
  initialize(absl::nullopt);

  // MockTimer constructor sets up expectations in the Dispatcher class to wire it up
  MockTimer* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(*request_timer, enabled()).Times(AnyNumber());
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(*request_timer, disableTimer());
  sendImmediateResponse500();
  // The rest of the filter isn't necessarily called after this.
}

// An immediate response on the response path
TEST_F(OrderingTest, ImmediateResponseOnResponse) {
  initialize(absl::nullopt);

  MockTimer* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enabled()).Times(AnyNumber());
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(*request_timer, disableTimer());
  sendRequestHeadersReply();

  MockTimer* response_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*response_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(*response_timer, enabled()).Times(AnyNumber());
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(*response_timer, disableTimer());
  sendImmediateResponse500();
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
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

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
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

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
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

  Buffer::OwnedImpl req_body_1("Hello!");
  Buffer::OwnedImpl req_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&req_buffer));
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillRepeatedly(Invoke(
          [&req_buffer](Buffer::Instance& new_chunk, Unused) { req_buffer.add(new_chunk); }));

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  sendRequestHeadersReply();
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body_1, true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendResponseBodyReply(); // Wrong message here

  // Expect us to go on from here normally but send no more stream messages
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// Receive a request headers reply in response to the response
// headers message -- should continue without error.
TEST_F(OrderingTest, IncorrectResponseHeadersReply) {
  initialize(absl::nullopt);

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestHeadersReply();

  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  sendRequestHeadersReply();
  // Still should ignore the message and go on but send no more stream messages
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
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
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
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
  initialize([](ExternalProcessor& proto_config) {
    proto_config.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(*request_timer, enabled()).WillOnce(Return(true));
  EXPECT_CALL(*request_timer, disableTimer());
  sendGrpcError();
  // The rest of the filter isn't called after this.
}

// gRPC error in response to message results in connection being dropped
// if failures are ignored
TEST_F(OrderingTest, GrpcErrorInlineIgnored) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    cfg.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(*request_timer, enabled()).WillOnce(Return(true));
  EXPECT_CALL(*request_timer, disableTimer());
  sendGrpcError();

  // After that we ignore the processor
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC error in between calls should still be delivered
TEST_F(OrderingTest, GrpcErrorOutOfLine) {
  initialize([](ExternalProcessor& cfg) {
    cfg.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_CALL(*request_timer, disableTimer());
  sendRequestHeadersReply();

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_CALL(*request_timer, enabled()).WillOnce(Return(false));
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
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC error might be received after a message timeout has fired
TEST_F(OrderingTest, GrpcErrorAfterTimeout) {
  initialize([](ExternalProcessor& proto_config) {
    proto_config.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersGet(true);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  request_timer->invokeCallback();
  // Nothing should happen now despite the gRPC error
  sendGrpcError();
}

// Allow the timeout to expire before the response body response can be sent
TEST_F(OrderingTest, TimeoutOnResponseBody) {
  initialize([](ExternalProcessor& cfg) {
    cfg.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(*request_timer, disableTimer());
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body;
  req_body.add("Dummy data 1");
  Buffer::OwnedImpl buffered_request;
  expectBufferedRequest(buffered_request, true);

  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body, true));
  EXPECT_CALL(*request_timer, disableTimer());
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  sendRequestBodyReply();

  Buffer::OwnedImpl resp_body("Dummy response");
  Buffer::OwnedImpl buffered_response;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&buffered_response));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillRepeatedly(Invoke([&buffered_response](Buffer::Instance& new_chunk, Unused) {
        buffered_response.add(new_chunk);
      }));
  auto* response_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*response_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendResponseHeaders(true);
  EXPECT_CALL(*response_timer, disableTimer());
  sendResponseHeadersReply();
  EXPECT_CALL(*response_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_body, true));

  // Now, fire the timeout, which will end everything
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  response_timer->invokeCallback();
}

// Allow the timeout to expire before the request body response can be sent
TEST_F(OrderingTest, TimeoutOnRequestBody) {
  initialize([](ExternalProcessor& cfg) {
    cfg.mutable_message_timeout()->set_nanos(kMessageTimeoutNanos);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  auto* request_timer = new MockTimer(&dispatcher_);
  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  sendRequestHeadersPost(true);
  EXPECT_CALL(*request_timer, disableTimer());
  sendRequestHeadersReply();

  Buffer::OwnedImpl req_body;
  req_body.add("Dummy data 1");
  Buffer::OwnedImpl buffered_request;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&buffered_request));
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillRepeatedly(Invoke([&buffered_request](Buffer::Instance& new_chunk, Unused) {
        buffered_request.add(new_chunk);
      }));

  EXPECT_CALL(*request_timer, enableTimer(kMessageTimeout, nullptr));
  EXPECT_CALL(stream_delegate_, send(_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body, true));

  // Now fire the timeout and expect a 500 error
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  request_timer->invokeCallback();
}

// gRPC failure while opening stream
TEST_F(FastFailOrderingTest, GrpcErrorOnStartRequestHeaders) {
  initialize(absl::nullopt);
  HttpTestUtility::addDefaultHeaders(request_headers_, "GET");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
}

// gRPC failure while opening stream with errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartRequestHeaders) {
  initialize([](ExternalProcessor& cfg) { cfg.set_failure_mode_allow(true); });
  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedRequest(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only request body enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartRequestBody) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
  });
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  Buffer::OwnedImpl buffered_body;
  expectBufferedRequest(buffered_body, false);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body, true));
}

// gRPC failure while opening stream with only request body enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartRequestBodyBufferedPartial) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  });
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(BufferSize));
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body, true));
}

// gRPC failure while opening stream with only request body enabled in streaming mode
TEST_F(FastFailOrderingTest, GrpcErrorOnStartRequestBodyStreaming) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::STREAMED);
  });
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  Buffer::OwnedImpl buffered_body;
  expectBufferedRequest(buffered_body, false);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_body, true));
}

// gRPC failure while opening stream with only request body enabled and errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartRequestBody) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::BUFFERED);
  });
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  Buffer::OwnedImpl buffered_body;
  expectBufferedRequest(buffered_body, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_body, true));
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedRequest(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only request body enabled and errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartRequestBodyBufferedPartial) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::BUFFERED_PARTIAL);
  });
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(BufferSize));
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_body, true));
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only request body enabled in streamed mode and errors
// ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartRequestBodyStreamed) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_request_body_mode(ProcessingMode::STREAMED);
  });
  sendRequestHeadersPost(false);
  Buffer::OwnedImpl req_body("Hello!");
  Buffer::OwnedImpl buffered_body;
  expectBufferedRequest(buffered_body, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_body, true));
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedRequest(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only response headers enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartResponseHeaders) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
  });

  sendRequestHeadersGet(false);
  response_headers_.setStatus(200);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
}

// gRPC failure while opening stream with only response headers enabled and errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartResponseHeaders) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedRequest(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only response body enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartResponseBody) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedResponse(resp_buf, false);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only response body enabled and errors are ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartResponseBody) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_body_mode(ProcessingMode::BUFFERED);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedResponse(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only response body enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartResponseTrailers) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_trailer_mode(ProcessingMode::SEND);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedResponse(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, false));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
}

// gRPC failure while opening stream with only response body enabled but errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartResponseTrailers) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_trailer_mode(ProcessingMode::SEND);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Buffer::OwnedImpl resp_buf;
  expectBufferedResponse(resp_buf, false);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
}

// gRPC failure while opening stream with only response body enabled
TEST_F(FastFailOrderingTest, GrpcErrorOnStartAddResponseTrailers) {
  initialize([](ExternalProcessor& cfg) {
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_trailer_mode(ProcessingMode::SEND);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(response_trailers));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_body, true));
}

// gRPC failure while opening stream with only response body enabled but errors ignored
TEST_F(FastFailOrderingTest, GrpcErrorIgnoredOnStartAddResponseTrailers) {
  initialize([](ExternalProcessor& cfg) {
    cfg.set_failure_mode_allow(true);
    auto* pm = cfg.mutable_processing_mode();
    pm->set_request_header_mode(ProcessingMode::SKIP);
    pm->set_response_header_mode(ProcessingMode::SKIP);
    pm->set_response_trailer_mode(ProcessingMode::SEND);
  });

  sendRequestHeadersGet(false);
  sendResponseHeaders(false);
  Buffer::OwnedImpl resp_body("Hello!");
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(response_trailers));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_body, true));
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
