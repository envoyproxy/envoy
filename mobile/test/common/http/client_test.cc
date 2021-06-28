#include <atomic>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/http/common.h"
#include "test/common/mocks/event/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/api_listener.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/http/client.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Http {

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers) {
  ResponseHeaderMapPtr transformed_headers = ResponseHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        LowerCaseString(Data::Utility::copyToString(headers.entries[i].key)),
        Data::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

class ClientTest : public testing::Test {
public:
  typedef struct {
    uint32_t on_headers_calls;
    uint32_t on_data_calls;
    uint32_t on_trailers_calls;
    uint32_t on_complete_calls;
    uint32_t on_error_calls;
    uint32_t on_cancel_calls;
    std::string expected_status_;
    bool end_stream_with_headers_;
  } callbacks_called;

  ClientTest() {
    bridge_callbacks_.context = &cc_;

    // Set up default bridge callbacks. Indivividual tests can override.
    bridge_callbacks_.on_complete = [](void* context) -> void* {
      callbacks_called* cc = static_cast<callbacks_called*>(context);
      cc->on_complete_calls++;
      return nullptr;
    };
    bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool end_stream,
                                      void* context) -> void* {
      ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
      callbacks_called* cc = static_cast<callbacks_called*>(context);
      EXPECT_EQ(end_stream, cc->end_stream_with_headers_);
      EXPECT_EQ(response_headers->Status()->value().getStringView(), cc->expected_status_);
      cc->on_headers_calls++;
      return nullptr;
    };
    bridge_callbacks_.on_error = [](envoy_error, void* context) -> void* {
      callbacks_called* cc = static_cast<callbacks_called*>(context);
      cc->on_error_calls++;
      return nullptr;
    };
    bridge_callbacks_.on_data = [](envoy_data c_data, bool, void* context) -> void* {
      callbacks_called* cc = static_cast<callbacks_called*>(context);
      cc->on_data_calls++;
      c_data.release(c_data.context);
      return nullptr;
    };
    bridge_callbacks_.on_cancel = [](void* context) -> void* {
      callbacks_called* cc = static_cast<callbacks_called*>(context);
      cc->on_cancel_calls++;
      return nullptr;
    };
  }

  envoy_headers defaultRequestHeaders() {
    // Build a set of request headers.
    TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    return Utility::toBridgeHeaders(headers);
  }

  void createStream() {
    // Create a stream.
    ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

    // Grab the response encoder in order to dispatch responses on the stream.
    // Return the request decoder to make sure calls are dispatched to the decoder via the
    // dispatcher API.
    EXPECT_CALL(api_listener_, newStream(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder_ = &encoder;
          return request_decoder_;
        }));
    http_client_.startStream(stream_, bridge_callbacks_);
  }

  MockApiListener api_listener_;
  MockRequestDecoder request_decoder_;
  ResponseEncoder* response_encoder_{};
  NiceMock<Event::MockProvisionalDispatcher> dispatcher_;
  envoy_http_callbacks bridge_callbacks_;
  callbacks_called cc_ = {0, 0, 0, 0, 0, 0, "200", true};
  std::atomic<envoy_network_t> preferred_network_{ENVOY_NET_GENERIC};
  uint64_t alt_cluster_ = 0;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  Client http_client_{api_listener_, dispatcher_, stats_store_, preferred_network_, random_};
  envoy_stream_t stream_ = 1;
};

TEST_F(ClientTest, SetDestinationCluster) {
  ON_CALL(random_, random()).WillByDefault(ReturnPointee(&alt_cluster_));

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers. Sending multiple headers is illegal and the upstream codec would not
  // accept it. However, given we are just trying to test preferred network headers and using mocks
  // this is fine.

  TestRequestHeaderMapImpl headers1;
  HttpTestUtility::addDefaultHeaders(headers1);
  headers1.setScheme("https");
  envoy_headers c_headers1 = Utility::toBridgeHeaders(headers1);

  preferred_network_.store(ENVOY_NET_GENERIC);

  TestRequestHeaderMapImpl expected_headers1{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers1), false));
  http_client_.sendHeaders(stream_, c_headers1, false);

  TestRequestHeaderMapImpl headers2;
  HttpTestUtility::addDefaultHeaders(headers2);
  headers2.setScheme("https");
  envoy_headers c_headers2 = Utility::toBridgeHeaders(headers2);

  preferred_network_.store(ENVOY_NET_WLAN);
  alt_cluster_ = 1;

  TestRequestHeaderMapImpl expected_headers2{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_wlan_alt"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers2), false));
  http_client_.sendHeaders(stream_, c_headers2, false);

  TestRequestHeaderMapImpl headers3;
  HttpTestUtility::addDefaultHeaders(headers3);
  headers3.setScheme("https");
  envoy_headers c_headers3 = Utility::toBridgeHeaders(headers3);

  preferred_network_.store(ENVOY_NET_WWAN);
  alt_cluster_ = 0;

  TestRequestHeaderMapImpl expected_headers3{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_wwan"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers3), true));
  http_client_.sendHeaders(stream_, c_headers3, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, SetDestinationClusterUpstreamProtocol) {
  ON_CALL(random_, random()).WillByDefault(ReturnPointee(&alt_cluster_));

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers. Sending multiple headers is illegal and the upstream codec would not
  // accept it. However, given we are just trying to test preferred network headers and using mocks
  // this is fine.

  TestRequestHeaderMapImpl headers1{{"x-envoy-mobile-upstream-protocol", "http2"}};
  HttpTestUtility::addDefaultHeaders(headers1);
  headers1.setScheme("https");
  envoy_headers c_headers1 = Utility::toBridgeHeaders(headers1);

  preferred_network_.store(ENVOY_NET_GENERIC);
  alt_cluster_ = 1;

  TestResponseHeaderMapImpl expected_headers1{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_h2_alt"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers1), false));
  http_client_.sendHeaders(stream_, c_headers1, false);

  TestRequestHeaderMapImpl headers2{{"x-envoy-mobile-upstream-protocol", "http2"}};
  HttpTestUtility::addDefaultHeaders(headers2);
  headers2.setScheme("https");
  envoy_headers c_headers2 = Utility::toBridgeHeaders(headers2);

  preferred_network_.store(ENVOY_NET_WLAN);
  alt_cluster_ = 0;

  TestResponseHeaderMapImpl expected_headers2{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_wlan_h2"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers2), false));
  http_client_.sendHeaders(stream_, c_headers2, false);

  TestRequestHeaderMapImpl headers3{{"x-envoy-mobile-upstream-protocol", "http2"}};
  HttpTestUtility::addDefaultHeaders(headers3);
  headers3.setScheme("https");
  envoy_headers c_headers3 = Utility::toBridgeHeaders(headers3);

  preferred_network_.store(ENVOY_NET_WWAN);
  alt_cluster_ = 1;

  TestResponseHeaderMapImpl expected_headers3{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_wwan_h2_alt"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers3), true));
  http_client_.sendHeaders(stream_, c_headers3, true);

  // Setting http1.
  TestRequestHeaderMapImpl headers4{{"x-envoy-mobile-upstream-protocol", "http1"}};
  HttpTestUtility::addDefaultHeaders(headers4);
  headers4.setScheme("https");
  envoy_headers c_headers4 = Utility::toBridgeHeaders(headers4);

  preferred_network_.store(ENVOY_NET_WWAN);
  alt_cluster_ = 0;

  TestResponseHeaderMapImpl expected_headers4{
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "host"},
      {":path", "/"},
      {"x-envoy-mobile-cluster", "base_wwan"},
      {"x-forwarded-proto", "https"},
  };
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers4), true));
  http_client_.sendHeaders(stream_, c_headers4, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, BasicStreamHeaders) {
  envoy_headers c_headers = defaultRequestHeaders();

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, BasicStreamData) {
  cc_.end_stream_with_headers_ = false;

  bridge_callbacks_.on_data = [](envoy_data c_data, bool end_stream, void* context) -> void* {
    EXPECT_TRUE(end_stream);
    EXPECT_EQ(Data::Utility::copyToString(c_data), "response body");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_data_calls++;
    c_data.release(c_data.context);
    return nullptr;
  };

  // Build body data
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request data. Although HTTP would need headers before data this unit test only wants to
  // test data functionality.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeData(BufferStringEqual("request body"), true));
  http_client_.sendData(stream_, c_data, true);

  // Encode response data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::InstancePtr response_data{new Buffer::OwnedImpl("response body")};
  response_encoder_->encodeData(*response_data, true);
  ASSERT_EQ(cc_.on_data_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, BasicStreamTrailers) {
  bridge_callbacks_.on_trailers = [](envoy_headers c_trailers, void* context) -> void* {
    ResponseHeaderMapPtr response_trailers = toResponseHeaders(c_trailers);
    EXPECT_EQ(response_trailers->get(LowerCaseString("x-test-trailer"))[0]->value().getStringView(),
              "test_trailer");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_trailers_calls++;
    return nullptr;
  };

  // Build a set of request trailers.
  TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Utility::toBridgeHeaders(trailers);

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request trailers. Although HTTP would need headers before trailers this unit test only
  // wants to test trailers functionality.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  http_client_.sendTrailers(stream_, c_trailers);

  // Encode response trailers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test_trailer"}};
  response_encoder_->encodeTrailers(response_trailers);
  ASSERT_EQ(cc_.on_trailers_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, MultipleDataStream) {
  cc_.end_stream_with_headers_ = false;

  bridge_callbacks_.on_data = [](envoy_data data, bool, void* context) -> void* {
    // TODO: assert end_stream and contents of c_data for multiple calls of on_data.
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_data_calls++;
    data.release(data.context);
    return nullptr;
  };

  envoy_headers c_headers = defaultRequestHeaders();

  // Build first body data
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  // Build second body data
  Buffer::OwnedImpl request_data2 = Buffer::OwnedImpl("request body2");
  envoy_data c_data2 = Data::Utility::toBridgeData(request_data2);

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  http_client_.sendHeaders(stream_, c_headers, false);

  // Send request data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeData(BufferStringEqual("request body"), false));
  http_client_.sendData(stream_, c_data, false);

  // Send second request data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeData(BufferStringEqual("request body2"), true));
  http_client_.sendData(stream_, c_data2, true);

  // Encode response headers and data.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  Buffer::InstancePtr response_data{new Buffer::OwnedImpl("response body")};
  response_encoder_->encodeData(*response_data, false);
  ASSERT_EQ(cc_.on_data_calls, 1);

  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  Buffer::InstancePtr response_data2{new Buffer::OwnedImpl("response body2")};
  response_encoder_->encodeData(*response_data2, true);
  ASSERT_EQ(cc_.on_data_calls, 2);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, MultipleStreams) {
  envoy_stream_t stream1 = 1;
  envoy_stream_t stream2 = 2;
  // Start stream1.

  envoy_headers c_headers = defaultRequestHeaders();

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream1, c_headers, true);

  // Start stream2.
  // Setup bridge_callbacks_ to handle the response headers.
  NiceMock<MockRequestDecoder> request_decoder2;
  ResponseEncoder* response_encoder2{};
  envoy_http_callbacks bridge_callbacks_2;
  callbacks_called cc2 = {0, 0, 0, 0, 0, 0, "200", true};
  bridge_callbacks_2.context = &cc2;
  bridge_callbacks_2.on_headers = [](envoy_headers c_headers, bool end_stream,
                                     void* context) -> void* {
    EXPECT_TRUE(end_stream);
    ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    bool* on_headers_called2 = static_cast<bool*>(context);
    *on_headers_called2 = true;
    return nullptr;
  };
  bridge_callbacks_2.on_complete = [](void* context) -> void* {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete_calls++;
    return nullptr;
  };

  envoy_headers c_headers2 = defaultRequestHeaders();

  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Grab the response encoder in order to dispatch responses on the stream.
  // Return the request decoder to make sure calls are dispatched to the decoder via the dispatcher
  // API.
  EXPECT_CALL(api_listener_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder2 = &encoder;
        return request_decoder2;
      }));
  http_client_.startStream(stream2, bridge_callbacks_2);

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream2, c_headers2, true);

  // Finish stream 2.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers2{{":status", "200"}};
  response_encoder2->encodeHeaders(response_headers2, true);
  ASSERT_EQ(cc2.on_headers_calls, 1);
  // Ensure that the on_headers on the bridge_callbacks_ was called.
  ASSERT_EQ(cc2.on_complete_calls, 1);

  // Finish stream 1.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_F(ClientTest, EnvoyLocalReplyNotAnError) {
  cc_.expected_status_ = "503";

  envoy_headers c_headers = defaultRequestHeaders();

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers. A non-200 code triggers an on_error callback chain. In particular, a
  // 503 should have an ENVOY_CONNECTION_FAILURE error code.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  response_encoder_->encodeHeaders(response_headers, true);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_error_calls, 0);
}

TEST_F(ClientTest, EnvoyLocalReplyNon503NotAnError) {
  cc_.expected_status_ = "504";

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers. A non-200 code triggers an on_error callback chain. In particular, a
  // non-503 should have an ENVOY_UNDEFINED_ERROR error code.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "504"}};
  response_encoder_->encodeHeaders(response_headers, true);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_error_calls, 0);
}

TEST_F(ClientTest, EnvoyResponseWithErrorCode) {
  cc_.expected_status_ = "218";
  // Override the on_error default with some custom checks.
  bridge_callbacks_.on_error = [](envoy_error error, void* context) -> void* {
    EXPECT_EQ(error.error_code, ENVOY_CONNECTION_FAILURE);
    EXPECT_EQ(error.attempt_count, 123);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error_calls++;
    error.message.release(error.message.context);
    return nullptr;
  };

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers. A non-200 code triggers an on_error callback chain. In particular, a
  // 503 should have an ENVOY_CONNECTION_FAILURE error code.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{
      {":status", "218"},
      {"x-internal-error-code", std::to_string(ENVOY_CONNECTION_FAILURE)},
      {"x-internal-error-message", "no internet"},
      {"x-envoy-attempt-count", "123"},
  };
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 0);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_error_calls, 1);
}

TEST_F(ClientTest, ResetStreamLocal) {
  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  http_client_.cancelStream(stream_);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
}

TEST_F(ClientTest, DoubleResetStreamLocal) {
  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(2);
  EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(2);
  http_client_.cancelStream(stream_);

  // Second cancel call has no effect because stream is already cancelled.
  http_client_.cancelStream(stream_);

  ASSERT_EQ(cc_.on_cancel_calls, 1);
  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
}

TEST_F(ClientTest, RemoteResetAfterStreamStart) {
  cc_.end_stream_with_headers_ = false;

  bridge_callbacks_.on_error = [](envoy_error error, void* context) -> void* {
    EXPECT_EQ(error.error_code, ENVOY_STREAM_RESET);
    EXPECT_EQ(error.message.length, 0);
    EXPECT_EQ(error.attempt_count, -1);
    // This will use envoy_noop_release.
    error.message.release(error.message.context);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error_calls++;
    return nullptr;
  };

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Used to verify that when a reset is received, the Http::Client::DirectStream fires
  // runResetCallbacks. The Http::ConnectionManager depends on the Http::Client::DirectStream
  // firing this tight loop to let the Http::ConnectionManager clean up its stream state.
  Http::MockStreamCallbacks callbacks;
  response_encoder_->getStream().addCallbacks(callbacks);

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(cc_.on_headers_calls, 1);

  // Expect that when a reset is received, the Http::Client::DirectStream fires
  // runResetCallbacks. The Http::ConnectionManager depends on the Http::Client::DirectStream
  // firing this tight loop to let the Http::ConnectionManager clean up its stream state.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  response_encoder_->getStream().resetStream(StreamResetReason::RemoteReset);
  // Ensure that the on_error on the bridge_callbacks_ was called.
  ASSERT_EQ(cc_.on_error_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 0);
}

TEST_F(ClientTest, StreamResetAfterOnComplete) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  // Ensure that the callbacks on the bridge_callbacks_ were called.
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // Cancellation should have no effect as the stream should have already been cleaned up.
  http_client_.cancelStream(stream_);
  ASSERT_EQ(cc_.on_cancel_calls, 0);
}

TEST_F(ClientTest, ResetWhenRemoteClosesBeforeLocal) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, true);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // Fire stream reset because Envoy does not allow half-open streams on the local side.
  response_encoder_->getStream().resetStream(StreamResetReason::RemoteReset);
  ASSERT_EQ(cc_.on_error_calls, 0);
}

TEST_F(ClientTest, Encode100Continue) {
  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode 100 continue should blow up.
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_DEATH(response_encoder_->encode100ContinueHeaders(response_headers),
               "panic: not implemented");
}

TEST_F(ClientTest, EncodeMetadata) {
  cc_.end_stream_with_headers_ = false;

  // Create a stream, and set up request_decoder_ and response_encoder_
  createStream();

  // Send request headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  envoy_headers c_headers = defaultRequestHeaders();
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  http_client_.sendHeaders(stream_, c_headers, true);

  // Encode response headers.
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  ASSERT_EQ(cc_.on_headers_calls, 1);

  MetadataMap metadata_map = {{"key", "value"}};
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  EXPECT_DEATH(response_encoder_->encodeMetadata(metadata_map_vector), "panic: not implemented");
}

TEST_F(ClientTest, NullAccessors) {
  envoy_stream_t stream = 1;
  envoy_http_callbacks bridge_callbacks;

  // Create a stream.
  ON_CALL(dispatcher_, isThreadSafe()).WillByDefault(Return(true));

  // Grab the response encoder in order to dispatch responses on the stream.
  // Return the request decoder to make sure calls are dispatched to the decoder via the dispatcher
  // API.
  EXPECT_CALL(api_listener_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder_ = &encoder;
        return request_decoder_;
      }));
  http_client_.startStream(stream, bridge_callbacks);

  EXPECT_FALSE(response_encoder_->http1StreamEncoderOptions().has_value());
  EXPECT_FALSE(response_encoder_->streamErrorOnInvalidHttpMessage());
}

} // namespace Http
} // namespace Envoy
