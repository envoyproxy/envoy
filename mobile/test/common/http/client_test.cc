#include <atomic>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/http/common.h"
#include "test/common/mocks/common/mocks.h"
#include "test/common/mocks/event/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/api_listener.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/bridge/utility.h"
#include "library/common/http/client.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

using testing::_;
using testing::AnyNumber;
using testing::ContainsRegex;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Http {

enum FlowControl {
  // Run with explicit flow control false.
  OFF,
  // Run with explicit flow control true, and generally call resumeData call
  // before data / fin / trailers arrive.
  EARLY_RESUME,
  // Run with explicit flow control true, and generally call resumeData after
  // data / fin / trailers arrive.
  LATE_RESUME,
};

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  ResponseHeaderMapPtr transformed_headers = ResponseHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        LowerCaseString(Bridge::Utility::copyToString(headers.entries[i].key)),
        Bridge::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

class TestHandle : public RequestDecoderHandle {
public:
  explicit TestHandle(MockRequestDecoder& decoder) : decoder_(decoder) {}

  ~TestHandle() override = default;

  OptRef<RequestDecoder> get() override { return {decoder_}; }

private:
  MockRequestDecoder& decoder_;
};

class ClientTest : public testing::TestWithParam<FlowControl> {
protected:
  ClientTest() {
    helper_handle_ = test::SystemHelperPeer::replaceSystemHelper();
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(dispatcher_, post_(_)).Times(AnyNumber()).WillRepeatedly([](Event::PostCb cb) {
      cb();
      return ENVOY_SUCCESS;
    });
  }

  struct StreamCallbacksCalled {
    uint32_t on_headers_calls_{0};
    uint32_t on_data_calls_{0};
    uint32_t on_trailers_calls_{0};
    uint32_t on_complete_calls_{0};
    uint32_t on_error_calls_{0};
    uint32_t on_cancel_calls_{0};
    uint32_t on_send_window_available_calls_{0};
    std::string expected_status_{"200"};
    bool end_stream_with_headers_{true};
    std::string body_data_{""};
  };

  RequestHeaderMapPtr createDefaultRequestHeaders() {
    auto headers = Utility::createRequestHeaderMapPtr();
    HttpTestUtility::addDefaultHeaders(*headers);
    return headers;
  }

  EnvoyStreamCallbacks createDefaultStreamCallbacks(StreamCallbacksCalled& callbacks_called) {
    EnvoyStreamCallbacks stream_callbacks;

    stream_callbacks.on_headers_ = [&](const ResponseHeaderMap& headers, bool end_stream,
                                       envoy_stream_intel) -> void {
      EXPECT_EQ(end_stream, callbacks_called.end_stream_with_headers_);
      EXPECT_EQ(headers.Status()->value().getStringView(), callbacks_called.expected_status_);
      callbacks_called.on_headers_calls_++;
    };
    stream_callbacks.on_data_ = [&](const Buffer::Instance& buffer, uint64_t length,
                                    bool /* end_stream */, envoy_stream_intel) -> void {
      callbacks_called.on_data_calls_++;
      std::string response_body(length, ' ');
      buffer.copyOut(0, length, response_body.data());
      callbacks_called.body_data_ += response_body;
    };
    stream_callbacks.on_trailers_ = [&](const ResponseTrailerMap&, envoy_stream_intel) -> void {
      callbacks_called.on_trailers_calls_++;
    };
    stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) -> void {
      callbacks_called.on_complete_calls_++;
    };
    stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                     envoy_final_stream_intel) -> void {
      callbacks_called.on_error_calls_++;
    };
    stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) -> void {
      callbacks_called.on_cancel_calls_++;
    };
    stream_callbacks.on_send_window_available_ = [&](envoy_stream_intel) -> void {
      callbacks_called.on_send_window_available_calls_++;
    };

    return stream_callbacks;
  }

  void createStream(EnvoyStreamCallbacks&& stream_callbacks) {
    ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));
    ON_CALL(*request_decoder_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

    // Grab the response encoder in order to dispatch responses on the stream.
    // Return the request decoder to make sure calls are dispatched to the decoder via the
    // dispatcher API.
    EXPECT_CALL(*api_listener_, newStreamHandle(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoderHandlePtr {
          response_encoder_ = &encoder;
          return std::make_unique<TestHandle>(*request_decoder_);
        }));
    http_client_.startStream(stream_, std::move(stream_callbacks), explicit_flow_control_ != OFF);
  }

  void resumeDataIfEarlyResume(int32_t bytes) {
    if (explicit_flow_control_ == EARLY_RESUME) {
      auto callbacks = dynamic_cast<Client::DirectStreamCallbacks*>(response_encoder_);
      callbacks->resumeData(bytes);
    }
  }

  void resumeDataIfLateResume(int32_t bytes) {
    if (explicit_flow_control_ == LATE_RESUME) {
      auto callbacks = dynamic_cast<Client::DirectStreamCallbacks*>(response_encoder_);
      callbacks->resumeData(bytes);
    }
  }

  void resumeData(int32_t bytes) {
    if (explicit_flow_control_ != OFF) {
      auto callbacks = dynamic_cast<Client::DirectStreamCallbacks*>(response_encoder_);
      callbacks->resumeData(bytes);
    }
  }

  std::unique_ptr<MockApiListener> owned_api_listener_ = std::make_unique<MockApiListener>();
  MockApiListener* api_listener_ = owned_api_listener_.get();
  std::unique_ptr<NiceMock<MockRequestDecoder>> request_decoder_{
      std::make_unique<NiceMock<MockRequestDecoder>>()};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  ResponseEncoder* response_encoder_{};
  NiceMock<Event::MockProvisionalDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  FlowControl explicit_flow_control_{GetParam()};
  Client http_client_{std::move(owned_api_listener_), dispatcher_, *stats_store_.rootScope(),
                      random_};
  envoy_stream_t stream_ = 1;
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
};

INSTANTIATE_TEST_SUITE_P(TestModes, ClientTest, ::testing::Values(OFF, EARLY_RESUME, LATE_RESUME));

TEST_P(ClientTest, BasicStreamHeaders) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, BasicStreamHeadersIdempotent) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  expected_headers.addCopy("x-envoy-mobile-cluster", "base_clear");
  expected_headers.addCopy("x-forwarded-proto", "https");
  expected_headers.addCopy("x-envoy-retry-on", "http3-post-connect-failure");
  EXPECT_CALL(*request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), /* end_stream= */ true,
                           /* idempotent= */ true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, BasicStreamData) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;
  auto stream_callbacks = createDefaultStreamCallbacks(callbacks_called);

  stream_callbacks.on_data_ = [&](const Buffer::Instance& buffer, uint64_t length, bool end_stream,
                                  envoy_stream_intel) -> void {
    EXPECT_TRUE(end_stream);
    std::string response_body(length, ' ');
    buffer.copyOut(0, length, response_body.data());
    EXPECT_EQ(response_body, "response body");
    callbacks_called.on_data_calls_++;
  };

  // Build body data
  auto request_data = std::make_unique<Buffer::OwnedImpl>("request body");

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(std::move(stream_callbacks));

  // Send request data. Although HTTP would need headers before data this unit test only wants to
  // test data functionality.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeData(BufferStringEqual("request body"), true));
  resumeDataIfEarlyResume(20); // Resume before data arrives.
  http_client_.sendData(stream_, std::move(request_data), true);
  resumeDataIfLateResume(20); // Resume after data arrives.

  // Encode response data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::OwnedImpl response_data("response body");
  response_encoder_->encodeData(response_data, true);
  ASSERT_EQ(callbacks_called.on_data_calls_, 1);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, BasicStreamTrailers) {
  StreamCallbacksCalled callbacks_called;
  auto stream_callbacks = createDefaultStreamCallbacks(callbacks_called);

  stream_callbacks.on_trailers_ = [&](const Http::ResponseTrailerMap& trailers,
                                      envoy_stream_intel) -> void {
    EXPECT_EQ(trailers.get(LowerCaseString("x-test-trailer"))[0]->value().getStringView(),
              "test_trailer");
    callbacks_called.on_trailers_calls_++;
  };

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(std::move(stream_callbacks));

  // Send request trailers. Although HTTP would need headers before trailers this unit test only
  // wants to test trailers functionality.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeTrailers_(_));
  resumeDataIfEarlyResume(20); // Resume before trailers arrive.
  http_client_.sendTrailers(stream_, Utility::createRequestTrailerMapPtr());
  resumeDataIfLateResume(20); // Resume after trailers arrive.

  // Encode response trailers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test_trailer"}};
  response_encoder_->encodeTrailers(response_trailers);
  ASSERT_EQ(callbacks_called.on_trailers_calls_, 1);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, MultipleDataStream) {
  Event::MockDispatcher dispatcher;
  ON_CALL(dispatcher_, drain).WillByDefault([&](Event::Dispatcher& event_dispatcher) {
    dispatcher_.Event::ProvisionalDispatcher::drain(event_dispatcher);
  });
  dispatcher_.drain(dispatcher);
  Event::MockSchedulableCallback* process_buffered_data_callback = nullptr;
  if (explicit_flow_control_) {
    process_buffered_data_callback = new Event::MockSchedulableCallback(&dispatcher);
    EXPECT_CALL(*process_buffered_data_callback, scheduleCallbackNextIteration());
    ON_CALL(dispatcher_, createSchedulableCallback).WillByDefault([&](std::function<void()> cb) {
      return dispatcher_.Event::ProvisionalDispatcher::createSchedulableCallback(cb);
    });
  }

  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Build first body data
  auto request_data1 = std::make_unique<Buffer::OwnedImpl>("request body1");
  // Build second body data
  auto request_data2 = std::make_unique<Buffer::OwnedImpl>("request body2");

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, false));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), false);

  // Send request data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeData(BufferStringEqual("request body1"), false));
  http_client_.sendData(stream_, std::move(request_data1), false);
  EXPECT_EQ(callbacks_called.on_send_window_available_calls_, 0);
  if (explicit_flow_control_) {
    EXPECT_TRUE(process_buffered_data_callback->enabled_);
    process_buffered_data_callback->invokeCallback();
    // The buffer is not full: expect an on_send_window_available call.
    EXPECT_EQ(callbacks_called.on_send_window_available_calls_, 1);
  }

  // Send second request data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeData(BufferStringEqual("request body2"), true));
  http_client_.sendData(stream_, std::move(request_data2), true);
  // The stream is done: no further on_send_window_available calls should happen.
  EXPECT_EQ(callbacks_called.on_send_window_available_calls_, explicit_flow_control_ ? 1 : 0);

  // Encode response headers and data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(3);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(3);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  Buffer::OwnedImpl response_data("response body");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data, false);
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 1);
  EXPECT_EQ("response body", callbacks_called.body_data_);

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::OwnedImpl response_data2("response body2");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data2, true);
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 2);
  EXPECT_EQ("response bodyresponse body2", callbacks_called.body_data_);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, EmptyDataWithEndStream) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  // Send request headers.
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers and data.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  Buffer::OwnedImpl response_data("response body");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data, false);
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 1);
  EXPECT_EQ("response body", callbacks_called.body_data_);

  // Make sure end of stream as communicated by an empty data with fin is
  // processed correctly.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::OwnedImpl response_data2("");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data2, true);
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 2);
  EXPECT_EQ("response body", callbacks_called.body_data_);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ClientTest, MultipleStreams) {
  envoy_stream_t stream1 = 1;
  envoy_stream_t stream2 = 2;

  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called1;
  auto stream_callbacks1 = createDefaultStreamCallbacks(callbacks_called1);
  createStream(std::move(stream_callbacks1));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream1, createDefaultRequestHeaders(), true);

  // Start stream2.
  // Setup EnvoyStreamCallbacks to handle the response headers.
  NiceMock<MockRequestDecoder> request_decoder2;
  ON_CALL(request_decoder2, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  ResponseEncoder* response_encoder2{};
  StreamCallbacksCalled callbacks_called2;
  auto stream_callbacks2 = createDefaultStreamCallbacks(callbacks_called1);
  stream_callbacks2.on_headers_ = [&](const ResponseHeaderMap& headers, bool end_stream,
                                      envoy_stream_intel) -> void {
    EXPECT_TRUE(end_stream);
    EXPECT_EQ(headers.Status()->value().getStringView(), "200");
    callbacks_called2.on_headers_calls_ = true;
  };
  stream_callbacks2.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) -> void {
    callbacks_called2.on_complete_calls_++;
  };

  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Grab the response encoder in order to dispatch responses on the stream.
  // Return the request decoder to make sure calls are dispatched to the decoder via the dispatcher
  // API.
  EXPECT_CALL(*api_listener_, newStreamHandle(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoderHandlePtr {
        response_encoder2 = &encoder;
        return std::make_unique<TestHandle>(request_decoder2);
      }));
  http_client_.startStream(stream2, std::move(stream_callbacks2), explicit_flow_control_);

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream2, createDefaultRequestHeaders(), true);

  // Finish stream 2.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers2{{":status", "200"}};
  response_encoder2->encodeHeaders(response_headers2, true);
  ASSERT_EQ(callbacks_called2.on_headers_calls_, 1);
  // Ensure that the on_headers on the EnvoyStreamCallbacks was called.
  ASSERT_EQ(callbacks_called2.on_complete_calls_, 1);

  // Finish stream 1.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called1.on_headers_calls_, 1);
  ASSERT_EQ(callbacks_called1.on_complete_calls_, 1);
}

TEST_P(ClientTest, MultipleUploads) {
  envoy_stream_t stream1 = 1;
  envoy_stream_t stream2 = 2;
  auto request_data1 = std::make_unique<Buffer::OwnedImpl>("request body1");
  auto request_data2 = std::make_unique<Buffer::OwnedImpl>("request body2");

  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called1;
  auto stream_callbacks1 = createDefaultStreamCallbacks(callbacks_called1);
  createStream(std::move(stream_callbacks1));

  // Send request headers.
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, false));
  http_client_.sendHeaders(stream1, createDefaultRequestHeaders(), false);
  http_client_.sendData(stream1, std::move(request_data1), true);

  // Start stream2.
  // Setup EnvoyStreamCallbacks to handle the response headers.
  NiceMock<MockRequestDecoder> request_decoder2;
  ON_CALL(request_decoder2, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  ResponseEncoder* response_encoder2{};
  StreamCallbacksCalled callbacks_called2;
  auto stream_callbacks2 = createDefaultStreamCallbacks(callbacks_called1);
  stream_callbacks2.on_headers_ = [&](const ResponseHeaderMap& headers, bool end_stream,
                                      envoy_stream_intel) -> void {
    EXPECT_TRUE(end_stream);
    EXPECT_EQ(headers.Status()->value().getStringView(), "200");
    callbacks_called2.on_headers_calls_ = true;
  };
  stream_callbacks2.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) -> void {
    callbacks_called2.on_complete_calls_++;
  };

  std::vector<Event::SchedulableCallback*> window_callbacks;
  ON_CALL(dispatcher_, createSchedulableCallback).WillByDefault([&](std::function<void()> cb) {
    Event::SchedulableCallbackPtr scheduler =
        dispatcher_.Event::ProvisionalDispatcher::createSchedulableCallback(cb);
    window_callbacks.push_back(scheduler.get());
    return scheduler;
  });

  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Grab the response encoder in order to dispatch responses on the stream.
  // Return the request decoder to make sure calls are dispatched to the decoder via the dispatcher
  // API.
  EXPECT_CALL(*api_listener_, newStreamHandle(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoderHandlePtr {
        response_encoder2 = &encoder;
        return std::make_unique<TestHandle>(request_decoder2);
      }));
  http_client_.startStream(stream2, std::move(stream_callbacks2), explicit_flow_control_);

  // Send request headers.
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, false));
  http_client_.sendHeaders(stream2, createDefaultRequestHeaders(), false);
  http_client_.sendData(stream2, std::move(request_data2), true);

  for (auto* callback : window_callbacks) {
    EXPECT_TRUE(callback->enabled());
  }

  // Finish stream 2.
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers2{{":status", "200"}};
  response_encoder2->encodeHeaders(response_headers2, true);
  ASSERT_EQ(callbacks_called2.on_headers_calls_, 1);
  // Ensure that the on_headers on the EnvoyStreamCallbacks was called.
  ASSERT_EQ(callbacks_called2.on_complete_calls_, 1);

  // Finish stream 1.
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called1.on_headers_calls_, 1);
  ASSERT_EQ(callbacks_called1.on_complete_calls_, 1);
}

TEST_P(ClientTest, EnvoyLocalError) {
  // Override the on_error default with some custom checks.
  StreamCallbacksCalled callbacks_called;
  auto stream_callbacks = createDefaultStreamCallbacks(callbacks_called);
  stream_callbacks.on_error_ = [&](const EnvoyError& error, envoy_stream_intel,
                                   envoy_final_stream_intel) -> void {
    EXPECT_EQ(error.error_code_, ENVOY_CONNECTION_FAILURE);
    EXPECT_THAT(error.message_, Eq("rc: 503|ec: 2|rsp_flags: "
                                   "4,26|http: 3|det: failed miserably"));
    EXPECT_EQ(error.attempt_count_, 123);
    callbacks_called.on_error_calls_++;
  };

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(std::move(stream_callbacks));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers. A non-200 code triggers an on_error callback chain. In particular, a
  // 503 should have an ENVOY_CONNECTION_FAILURE error code.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  stream_info_.setResponseCode(503);
  stream_info_.setResponseCodeDetails("failed miserably");
  stream_info_.setResponseFlag(StreamInfo::ResponseFlag(StreamInfo::UpstreamRemoteReset));
  stream_info_.setResponseFlag(StreamInfo::ResponseFlag(StreamInfo::DnsResolutionFailed));
  stream_info_.setAttemptCount(123);
  EXPECT_CALL(stream_info_, protocol()).WillRepeatedly(Return(Http::Protocol::Http3));
  response_encoder_->getStream().resetStream(Http::StreamResetReason::LocalConnectionFailure);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 0);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
  ASSERT_EQ(callbacks_called.on_error_calls_, 1);
}

TEST_P(ClientTest, ResetStreamLocal) {
  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  http_client_.cancelStream(stream_);
  ASSERT_EQ(callbacks_called.on_cancel_calls_, 1);
  ASSERT_EQ(callbacks_called.on_error_calls_, 0);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
}

TEST_P(ClientTest, DoubleResetStreamLocal) {
  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  http_client_.cancelStream(stream_);

  // Second cancel call has no effect because stream is already cancelled.
  http_client_.cancelStream(stream_);

  ASSERT_EQ(callbacks_called.on_cancel_calls_, 1);
  ASSERT_EQ(callbacks_called.on_error_calls_, 0);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
}

TEST_P(ClientTest, RemoteResetAfterStreamStart) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  auto stream_callbacks = createDefaultStreamCallbacks(callbacks_called);
  stream_callbacks.on_error_ = [&](const EnvoyError& error, envoy_stream_intel,
                                   envoy_final_stream_intel) -> void {
    EXPECT_EQ(error.error_code_, ENVOY_STREAM_RESET);
    EXPECT_THAT(error.message_, ContainsRegex("ec: 1"));
    EXPECT_EQ(error.attempt_count_, 0);
    callbacks_called.on_error_calls_++;
  };

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(std::move(stream_callbacks));

  // Used to verify that when a reset is received, the Http::Client::DirectStream fires
  // runResetCallbacks. The Http::ConnectionManager depends on the Http::Client::DirectStream
  // firing this tight loop to let the Http::ConnectionManager clean up its stream state.
  Http::MockStreamCallbacks callbacks;
  response_encoder_->getStream().addCallbacks(callbacks);

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);

  // Expect that when a reset is received, the Http::Client::DirectStream fires
  // runResetCallbacks. The Http::ConnectionManager depends on the Http::Client::DirectStream
  // firing this tight loop to let the Http::ConnectionManager clean up its stream state.
  resumeDataIfEarlyResume(3);
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  response_encoder_->getStream().resetStream(StreamResetReason::RemoteReset);
  resumeDataIfLateResume(3);
  // Ensure that the on_error on the EnvoyStreamCallbacks was called.
  ASSERT_EQ(callbacks_called.on_error_calls_, 1);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
}

TEST_P(ClientTest, StreamResetAfterOnComplete) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);

  // Cancellation should have no effect as the stream should have already been cleaned up.
  http_client_.cancelStream(stream_);
  ASSERT_EQ(callbacks_called.on_cancel_calls_, 0);
}

TEST_P(ClientTest, ResetWhenRemoteClosesBeforeLocal) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);

  // Fire stream reset because Envoy does not allow half-open streams on the local side.
  response_encoder_->getStream().resetStream(StreamResetReason::RemoteReset);
  ASSERT_EQ(callbacks_called.on_error_calls_, 0);
}

TEST_P(ClientTest, Encode100Continue) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  StreamCallbacksCalled callbacks_called;
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode 100 continue should blow up.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
// Death tests are not supported on iOS.
#ifndef TARGET_OS_IOS
  EXPECT_ENVOY_BUG(response_encoder_->encode1xxHeaders(response_headers),
                   "Unexpected 100 continue");
#endif
}

TEST_P(ClientTest, EncodeMetadata) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);

  MetadataMap metadata_map = {{"key", "value"}};
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
// Death tests are not supported on iOS.
#ifndef TARGET_OS_IOS
  EXPECT_ENVOY_BUG(response_encoder_->encodeMetadata(metadata_map_vector), "Unexpected metadata");
#endif
}

TEST_P(ClientTest, NullAccessors) {
  envoy_stream_t stream = 1;

  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Grab the response encoder in order to dispatch responses on the stream.
  // Return the request decoder to make sure calls are dispatched to the decoder via the dispatcher
  // API.
  EXPECT_CALL(*api_listener_, newStreamHandle(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoderHandlePtr {
        response_encoder_ = &encoder;
        return std::make_unique<TestHandle>(*request_decoder_);
      }));
  StreamCallbacksCalled callbacks_called;
  http_client_.startStream(stream, createDefaultStreamCallbacks(callbacks_called),
                           explicit_flow_control_);

  EXPECT_FALSE(response_encoder_->http1StreamEncoderOptions().has_value());
  EXPECT_FALSE(response_encoder_->streamErrorOnInvalidHttpMessage());
}

using ExplicitFlowControlTest = ClientTest;
INSTANTIATE_TEST_SUITE_P(TestEarlyResume, ExplicitFlowControlTest,
                         ::testing::Values(EARLY_RESUME, LATE_RESUME));

TEST_P(ExplicitFlowControlTest, ShortRead) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers and data.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);

  // Test partial reads. Get 5 bytes but only pass 3 up.
  Buffer::OwnedImpl response_data("12345");
  resumeDataIfEarlyResume(3);
  response_encoder_->encodeData(response_data, true);

  // The stream is closed from Envoy's perspective. Make sure sanitizers will catch
  // any access to the decoder.
  request_decoder_.reset();

  resumeDataIfLateResume(3);
  EXPECT_EQ("123", callbacks_called.body_data_);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);

  // Kick off more data, and the other two and the FIN should arrive.
  resumeData(3);
  EXPECT_EQ("12345", callbacks_called.body_data_);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ExplicitFlowControlTest, DataArrivedWhileBufferNonempty) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers and data.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);

  // Test partial reads. Get 5 bytes but only pass 3 up.
  Buffer::OwnedImpl response_data("12345");
  resumeDataIfEarlyResume(3);
  response_encoder_->encodeData(response_data, false);
  resumeDataIfLateResume(3);
  EXPECT_EQ("123", callbacks_called.body_data_);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);

  Buffer::OwnedImpl response_data2("678910");
  response_encoder_->encodeData(response_data2, true);
  // The stream is closed from Envoy's perspective. Make sure sanitizers will catch
  // any access to the decoder.
  request_decoder_.reset();

  resumeData(20);
  EXPECT_EQ("12345678910", callbacks_called.body_data_);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ExplicitFlowControlTest, ResumeWithFin) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  // Send request headers.
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers and data.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  Buffer::OwnedImpl response_data("response body");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data, false);
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 1);
  EXPECT_EQ("response body", callbacks_called.body_data_);

  // Make sure end of stream as communicated by an empty data with end stream is
  // processed correctly regardless of if the resume is kicked off before the end stream.
  resumeDataIfEarlyResume(20);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::OwnedImpl response_data2("");
  response_encoder_->encodeData(response_data2, true);
  resumeDataIfLateResume(20);
  // The stream is closed from Envoy's perspective. Make sure sanitizers will catch
  // any access to the decoder.
  request_decoder_.reset();
  ASSERT_EQ(callbacks_called.on_data_calls_, 2);
  EXPECT_EQ("response body", callbacks_called.body_data_);
  // Ensure that the callbacks on the EnvoyStreamCallbacks were called.
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ExplicitFlowControlTest, ResumeWithDataAndTrailers) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  // Send request headers.
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers, data, and trailers.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(callbacks_called.on_headers_calls_, 1);
  Buffer::OwnedImpl response_data("response body");
  resumeDataIfEarlyResume(20);
  response_encoder_->encodeData(response_data, false);
  TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test_trailer"}};
  response_encoder_->encodeTrailers(response_trailers);
  // The stream is closed from Envoy's perspective. Make sure sanitizers will catch
  // any access to the decoder.
  request_decoder_.reset();

  // On the resume call, the data should be passed up, but not the trailers.
  resumeDataIfLateResume(20);
  ASSERT_EQ(callbacks_called.on_data_calls_, 1);
  ASSERT_EQ(callbacks_called.on_trailers_calls_, 0);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
  EXPECT_EQ("response body", callbacks_called.body_data_);

  EXPECT_TRUE(dispatcher_.to_delete_.empty());

  // On the next resume, trailers should be sent.
  resumeData(20);
  ASSERT_EQ(callbacks_called.on_trailers_calls_, 1);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 1);
}

TEST_P(ExplicitFlowControlTest, CancelWithStreamComplete) {
  StreamCallbacksCalled callbacks_called;
  callbacks_called.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream(createDefaultStreamCallbacks(callbacks_called));
  EXPECT_CALL(*request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, createDefaultRequestHeaders(), true);

  // Encode response headers and data.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);

  // When data arrives it should be buffered to send up
  Buffer::OwnedImpl response_data("12345");
  response_encoder_->encodeData(response_data, true);
  // The stream is closed from Envoy's perspective. Make sure sanitizers will catch
  // any access to the decoder.
  request_decoder_.reset();
  ASSERT_EQ(callbacks_called.on_complete_calls_, false);

  MockStreamCallbacks stream_callbacks;
  response_encoder_->getStream().addCallbacks(stream_callbacks);

  // make sure when the stream is canceled, the reset stream callbacks are
  // not called on the "closed" stream.
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(stream_callbacks, onResetStream(_, _)).Times(0);
  http_client_.cancelStream(stream_);
  ASSERT_EQ(callbacks_called.on_cancel_calls_, 1);
  ASSERT_EQ(callbacks_called.on_error_calls_, 0);
  ASSERT_EQ(callbacks_called.on_complete_calls_, 0);
}

} // namespace Http
} // namespace Envoy
