#include <cstdint>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/stats/stats.h"

#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Http {
namespace Http2 {

typedef ::testing::tuple<uint32_t, uint32_t, uint32_t, uint32_t> Http2SettingsTuple;
typedef ::testing::tuple<Http2SettingsTuple, Http2SettingsTuple> Http2SettingsTestParam;

namespace {
Http2Settings Http2SettingsFromTuple(const Http2SettingsTuple& tp) {
  Http2Settings ret;
  ret.hpack_table_size_ = ::testing::get<0>(tp);
  ret.max_concurrent_streams_ = ::testing::get<1>(tp);
  ret.initial_stream_window_size_ = ::testing::get<2>(tp);
  ret.initial_connection_window_size_ = ::testing::get<3>(tp);
  return ret;
}
} // namespace

class TestServerConnectionImpl : public ServerConnectionImpl {
public:
  TestServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings)
      : ServerConnectionImpl(connection, callbacks, scope, http2_settings) {}
  nghttp2_session* session() { return session_; }
  using ServerConnectionImpl::getStream;
};

class TestClientConnectionImpl : public ClientConnectionImpl {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings)
      : ClientConnectionImpl(connection, callbacks, scope, http2_settings) {}
  nghttp2_session* session() { return session_; }
  using ClientConnectionImpl::getStream;
};

class Http2CodecImplTest : public testing::TestWithParam<Http2SettingsTestParam> {
public:
  struct ConnectionWrapper {
    void dispatch(const Buffer::Instance& data, ConnectionImpl& connection) {
      buffer_.add(data);
      if (!dispatching_) {
        while (buffer_.length() > 0) {
          dispatching_ = true;
          connection.dispatch(buffer_);
          dispatching_ = false;
        }
      }
    }

    bool dispatching_{};
    Buffer::OwnedImpl buffer_;
  };

  Http2CodecImplTest()
      : client_http2settings_(Http2SettingsFromTuple(::testing::get<0>(GetParam()))),
        client_(client_connection_, client_callbacks_, stats_store_, client_http2settings_),
        server_http2settings_(Http2SettingsFromTuple(::testing::get<1>(GetParam()))),
        server_(server_connection_, server_callbacks_, stats_store_, server_http2settings_) {}

  void initialize() {
    request_encoder_ = &client_.newStream(response_decoder_);
    setupDefaultConnectionMocks();

    EXPECT_CALL(server_callbacks_, newStream(_))
        .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
          response_encoder_ = &encoder;
          encoder.getStream().addCallbacks(server_stream_callbacks_);
          return request_decoder_;
        }));
  }

  void setupDefaultConnectionMocks() {
    ON_CALL(client_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          server_wrapper_.dispatch(data, server_);
        }));
    ON_CALL(server_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          client_wrapper_.dispatch(data, client_);
        }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  const Http2Settings client_http2settings_;
  NiceMock<Network::MockConnection> client_connection_;
  MockConnectionCallbacks client_callbacks_;
  TestClientConnectionImpl client_;
  ConnectionWrapper client_wrapper_;
  const Http2Settings server_http2settings_;
  NiceMock<Network::MockConnection> server_connection_;
  MockServerConnectionCallbacks server_callbacks_;
  TestServerConnectionImpl server_;
  ConnectionWrapper server_wrapper_;
  MockStreamDecoder response_decoder_;
  StreamEncoder* request_encoder_;
  MockStreamDecoder request_decoder_;
  StreamEncoder* response_encoder_{};
  MockStreamCallbacks server_stream_callbacks_;
};

TEST_P(Http2CodecImplTest, ShutdownNotice) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  EXPECT_CALL(client_callbacks_, onGoAway());
  server_.shutdownNotice();
  server_.goAway();

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, ContinueHeaders) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
};

TEST_P(Http2CodecImplTest, InvalidContinueWithFin) {
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  response_encoder_->encodeHeaders(continue_headers, true);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, InvalidRepeatContinue) {
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  response_encoder_->encodeHeaders(continue_headers, true);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, Invalid103) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestHeaderMapImpl early_hint_headers{{":status", "103"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(early_hint_headers, false);

  EXPECT_THROW_WITH_MESSAGE(response_encoder_->encodeHeaders(early_hint_headers, false),
                            CodecProtocolException, "Unexpected 'trailers' with no end stream.");
  EXPECT_EQ(1, stats_store_.counter("http2.too_many_header_frames").value());
};

TEST_P(Http2CodecImplTest, Invalid204WithContentLength) {
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  TestHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "3"}};
  // What follows is a hack to get headers that should span into continuation frames. The default
  // maximum frame size is 16K. We will add 3,000 headers that will take us above this size and
  // not easily compress with HPACK. (I confirmed this generates 26,468 bytes of header data
  // which should contain a continuation.)
  for (uint i = 1; i < 3000; i++) {
    response_headers.addCopy(std::to_string(i), std::to_string(i));
  }

  response_encoder_->encodeHeaders(response_headers, false);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::RemoteReset));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, RefusedStreamReset) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalRefusedStreamReset));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);
}

TEST_P(Http2CodecImplTest, InvalidFrame) {
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));

  request_encoder_->encodeHeaders(TestHeaderMapImpl{}, true);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalReset));
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::RemoteReset));
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);
}

TEST_P(Http2CodecImplTest, TrailingHeaders) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, false);
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  request_encoder_->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  response_encoder_->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});
}

TEST_P(Http2CodecImplTest, TrailingHeadersLargeBody) {
  initialize();

  // Buffer server data so we can make sure we don't get any window updates.
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AtLeast(1));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  request_encoder_->encodeData(body, false);
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  request_encoder_->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});

  // Flush pending data.
  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  response_encoder_->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});
}

class Http2CodecImplDeferredResetTest : public Http2CodecImplTest {};

TEST_P(Http2CodecImplDeferredResetTest, DeferredResetClient) {
  initialize();

  InSequence s;

  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);

  // Do a request, but pause server dispatch so we don't send window updates. This will result in a
  // deferred reset, followed by a pending frames flush which will cause the stream to actually
  // be reset immediately since we are outside of dispatch context.
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_encoder_->encodeHeaders(request_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  EXPECT_CALL(client_stream_callbacks, onAboveWriteBufferHighWatermark()).Times(AnyNumber());
  request_encoder_->encodeData(body, true);
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::LocalReset));
  request_encoder_->getStream().resetStream(StreamResetReason::LocalReset);

  // Dispatch server. We expect to see some data.
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false)).WillOnce(InvokeWithoutArgs([&]() -> void {
    // Start a response inside the headers callback. This should not result in the client
    // seeing any headers as the stream should already be reset on the other side, even though
    // we don't know about it yet.
    TestHeaderMapImpl response_headers{{":status", "200"}};
    response_encoder_->encodeHeaders(response_headers, false);
  }));
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::RemoteReset));

  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);
}

TEST_P(Http2CodecImplDeferredResetTest, DeferredResetServer) {
  initialize();

  InSequence s;

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // In this case we do the same thing as DeferredResetClient but on the server side.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));
  TestHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(AnyNumber());
  response_encoder_->encodeData(body, true);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalReset));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);

  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::RemoteReset));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), client_);
}

class Http2CodecImplFlowControlTest : public Http2CodecImplTest {};

// Back up the pending_sent_data_ buffer in the client connection and make sure the watermarks fire
// as expected.
//
// This also tests the readDisable logic in StreamImpl, verifying that h2 bytes are consumed
// when the stream has readDisable(true) called.
TEST_P(Http2CodecImplFlowControlTest, TestFlowControlInPendingSendData) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Force the server stream to be read disabled. This will cause it to stop sending window
  // updates to the client.
  server_.getStream(1)->readDisable(true);

  uint32_t initial_stream_window =
      nghttp2_session_get_stream_effective_local_window_size(client_.session(), 1);
  // If this limit is changed, this test will fail due to the initial large writes being divided
  // into more than 4 frames. Fast fail here with this explanatory comment.
  ASSERT_EQ(65535, initial_stream_window);
  // Make sure the limits were configured properly in test set up.
  EXPECT_EQ(initial_stream_window, server_.getStream(1)->bufferLimit());
  EXPECT_EQ(initial_stream_window, client_.getStream(1)->bufferLimit());

  // One large write gets broken into smaller frames.
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AnyNumber());
  Buffer::OwnedImpl long_data(std::string(initial_stream_window, 'a'));
  request_encoder_->encodeData(long_data, false);

  // Verify that the window is full. The client will not send more data to the server for this
  // stream.
  EXPECT_EQ(0, nghttp2_session_get_stream_local_window_size(server_.session(), 1));
  EXPECT_EQ(0, nghttp2_session_get_stream_remote_window_size(client_.session(), 1));
  EXPECT_EQ(initial_stream_window, server_.getStream(1)->unconsumed_bytes_);

  // Now that the flow control window is full, further data causes the send buffer to back up.
  Buffer::OwnedImpl more_long_data(std::string(initial_stream_window, 'a'));
  request_encoder_->encodeData(more_long_data, false);
  EXPECT_EQ(initial_stream_window, client_.getStream(1)->pending_send_data_.length());
  EXPECT_EQ(initial_stream_window, server_.getStream(1)->unconsumed_bytes_);

  // If we go over the limit, the stream callbacks should fire.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl last_byte("!");
  request_encoder_->encodeData(last_byte, false);
  EXPECT_EQ(initial_stream_window + 1, client_.getStream(1)->pending_send_data_.length());

  // Now create a second stream on the connection.
  MockStreamDecoder response_decoder2;
  StreamEncoder* request_encoder2 = &client_.newStream(response_decoder_);
  StreamEncoder* response_encoder2;
  MockStreamCallbacks server_stream_callbacks2;
  MockStreamDecoder request_decoder2;
  // When the server stream is created it should check the status of the
  // underlying connection. Pretend it is overrun.
  EXPECT_CALL(server_connection_, aboveHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(server_stream_callbacks2, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder2 = &encoder;
        encoder.getStream().addCallbacks(server_stream_callbacks2);
        return request_decoder2;
      }));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, false));
  request_encoder2->encodeHeaders(request_headers, false);

  // Add the stream callbacks belatedly. On creation the stream should have
  // been noticed that the connection was backed up. Any new subscriber to
  // stream callbacks should get a callback when they addCallbacks.
  MockStreamCallbacks callbacks2;
  EXPECT_CALL(callbacks2, onAboveWriteBufferHighWatermark());
  request_encoder_->getStream().addCallbacks(callbacks2);

  // Add a third callback to make testing removal mid-watermark call below more interesting.
  MockStreamCallbacks callbacks3;
  EXPECT_CALL(callbacks3, onAboveWriteBufferHighWatermark());
  request_encoder_->getStream().addCallbacks(callbacks3);

  // Now unblock the server's stream. This will cause the bytes to be consumed, flow control
  // updates to be sent, and the client to flush all queued data.
  // For bonus corner case coverage, remove callback2 in the middle of runLowWatermarkCallbacks()
  // and ensure it is not called.
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([&]() -> void {
    request_encoder_->getStream().removeCallbacks(callbacks2);
  }));
  EXPECT_CALL(callbacks2, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(callbacks3, onBelowWriteBufferLowWatermark());
  server_.getStream(1)->readDisable(false);
  EXPECT_EQ(0, client_.getStream(1)->pending_send_data_.length());
  // The extra 1 byte sent won't trigger another window update, so the final window should be the
  // initial window minus the last 1 byte flush from the client to server.
  EXPECT_EQ(initial_stream_window - 1,
            nghttp2_session_get_stream_local_window_size(server_.session(), 1));
  EXPECT_EQ(initial_stream_window - 1,
            nghttp2_session_get_stream_remote_window_size(client_.session(), 1));
}

// Set up the same asTestFlowControlInPendingSendData, but tears the stream down with an early reset
// once the flow control window is full up.
TEST_P(Http2CodecImplFlowControlTest, EarlyResetRestoresWindow) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Force the server stream to be read disabled. This will cause it to stop sending window
  // updates to the client.
  server_.getStream(1)->readDisable(true);

  uint32_t initial_stream_window =
      nghttp2_session_get_stream_effective_local_window_size(client_.session(), 1);
  uint32_t initial_connection_window = nghttp2_session_get_remote_window_size(client_.session());
  // If this limit is changed, this test will fail due to the initial large writes being divided
  // into more than 4 frames. Fast fail here with this explanatory comment.
  ASSERT_EQ(65535, initial_stream_window);
  // One large write may get broken into smaller frames.
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AnyNumber());
  Buffer::OwnedImpl long_data(std::string(initial_stream_window, 'a'));
  // The one giant write will cause the buffer to go over the limit, then drain and go back under
  // the limit.
  request_encoder_->encodeData(long_data, false);

  // Verify that the window is full. The client will not send more data to the server for this
  // stream.
  EXPECT_EQ(0, nghttp2_session_get_stream_local_window_size(server_.session(), 1));
  EXPECT_EQ(0, nghttp2_session_get_stream_remote_window_size(client_.session(), 1));
  EXPECT_EQ(initial_stream_window, server_.getStream(1)->unconsumed_bytes_);
  EXPECT_GT(initial_connection_window, nghttp2_session_get_remote_window_size(client_.session()));

  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalRefusedStreamReset));
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset))
      .WillOnce(Invoke([&](StreamResetReason) -> void {
        // Test the case where the reset callbacks cause the socket to fill up,
        // causing the underlying connection to back up. Given the stream is
        // being destroyed the watermark callbacks should not fire (mocks for Times(0)
        // above)
        client_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
        client_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
        server_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
        server_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);

  // Regression test that the window is consumed even if the stream is destroyed early.
  EXPECT_EQ(initial_connection_window, nghttp2_session_get_remote_window_size(client_.session()));
}

// Test the HTTP2 pending_recv_data_ buffer going over and under watermark limits.
TEST_P(Http2CodecImplFlowControlTest, FlowControlPendingRecvData) {
  initialize();
  MockStreamCallbacks callbacks;

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Set artificially small watermarks to make the recv buffer easy to overrun. In production,
  // the recv buffer can be overrun by a client which negotiates a larger
  // SETTINGS_MAX_FRAME_SIZE but there's no current easy way to tweak that in
  // envoy (without sending raw HTTP/2 frames) so we lower the buffer limit instead.
  server_.getStream(1)->setWriteBufferWatermarks(10, 20);

  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl data(std::string(40, 'a'));
  request_encoder_->encodeData(data, false);
}

TEST_P(Http2CodecImplTest, WatermarkUnderEndStream) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // The 'true' on encodeData will set local_end_stream_ on the client but not
  // the server. Verify that client watermark callbacks will not be called, but
  // server callbacks may be called by simulating connection overflow on both
  // ends.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark());
  EXPECT_CALL(request_decoder_, decodeData(_, true)).WillOnce(InvokeWithoutArgs([&]() -> void {
    client_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
    client_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
    server_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
    server_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, true);

  // The 'true' on encodeData will set local_end_stream_ on the server. Verify
  // that neither client nor server watermark callbacks will be called again.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(HeaderMapEqual(&response_headers), true))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        client_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
        client_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
        server_.onUnderlyingConnectionAboveWriteBufferHighWatermark();
        server_.onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  response_encoder_->encodeHeaders(response_headers, true);
}

class Http2CodecImplStreamLimitTest : public Http2CodecImplTest {};

// Regression test for issue #3076.
//
// TODO(PiotrSikora): add tests that exercise both scenarios: before and after receiving
// the HTTP/2 SETTINGS frame.
TEST_P(Http2CodecImplStreamLimitTest, MaxClientStreams) {
  for (int i = 0; i < 101; ++i) {
    request_encoder_ = &client_.newStream(response_decoder_);
    setupDefaultConnectionMocks();
    EXPECT_CALL(server_callbacks_, newStream(_))
        .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
          response_encoder_ = &encoder;
          encoder.getStream().addCallbacks(server_stream_callbacks_);
          return request_decoder_;
        }));

    TestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers);
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
    request_encoder_->encodeHeaders(request_headers, true);
  }
}

#define HTTP2SETTINGS_SMALL_WINDOW_COMBINE                                                         \
  ::testing::Combine(::testing::Values(Http2Settings::DEFAULT_HPACK_TABLE_SIZE),                   \
                     ::testing::Values(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS),             \
                     ::testing::Values(Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE),             \
                     ::testing::Values(Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE))

// Deferred reset tests use only small windows so that we can test certain conditions.
INSTANTIATE_TEST_CASE_P(Http2CodecImplDeferredResetTest, Http2CodecImplDeferredResetTest,
                        ::testing::Combine(HTTP2SETTINGS_SMALL_WINDOW_COMBINE,
                                           HTTP2SETTINGS_SMALL_WINDOW_COMBINE));

// Flow control tests only use only small windows so that we can test certain conditions.
INSTANTIATE_TEST_CASE_P(Http2CodecImplFlowControlTest, Http2CodecImplFlowControlTest,
                        ::testing::Combine(HTTP2SETTINGS_SMALL_WINDOW_COMBINE,
                                           HTTP2SETTINGS_SMALL_WINDOW_COMBINE));

// we seperate default/edge cases here to avoid combinatorial explosion
#define HTTP2SETTINGS_DEFAULT_COMBINE                                                              \
  ::testing::Combine(::testing::Values(Http2Settings::DEFAULT_HPACK_TABLE_SIZE),                   \
                     ::testing::Values(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS),             \
                     ::testing::Values(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE),         \
                     ::testing::Values(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE))

// Stream limit test only uses the default values because not all combinations of
// edge settings allow for the number of streams needed by the test.
INSTANTIATE_TEST_CASE_P(Http2CodecImplStreamLimitTest, Http2CodecImplStreamLimitTest,
                        ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                           HTTP2SETTINGS_DEFAULT_COMBINE));

INSTANTIATE_TEST_CASE_P(Http2CodecImplTestDefaultSettings, Http2CodecImplTest,
                        ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                           HTTP2SETTINGS_DEFAULT_COMBINE));

#define HTTP2SETTINGS_EDGE_COMBINE                                                                 \
  ::testing::Combine(                                                                              \
      ::testing::Values(Http2Settings::MIN_HPACK_TABLE_SIZE, Http2Settings::MAX_HPACK_TABLE_SIZE), \
      ::testing::Values(Http2Settings::MIN_MAX_CONCURRENT_STREAMS,                                 \
                        Http2Settings::MAX_MAX_CONCURRENT_STREAMS),                                \
      ::testing::Values(Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE,                             \
                        Http2Settings::MAX_INITIAL_STREAM_WINDOW_SIZE),                            \
      ::testing::Values(Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE,                         \
                        Http2Settings::MAX_INITIAL_CONNECTION_WINDOW_SIZE))

INSTANTIATE_TEST_CASE_P(Http2CodecImplTestEdgeSettings, Http2CodecImplTest,
                        ::testing::Combine(HTTP2SETTINGS_EDGE_COMBINE, HTTP2SETTINGS_EDGE_COMBINE));

TEST(Http2CodecUtility, reconstituteCrumbledCookies) {
  {
    HeaderString key;
    HeaderString value;
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().ContentLength);
    HeaderString value;
    value.setInteger(5);
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().Cookie);
    HeaderString value;
    value.setCopy("a=b", 3);
    HeaderString cookies;
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_EQ(cookies, "a=b");

    HeaderString key2(Headers::get().Cookie);
    HeaderString value2;
    value2.setCopy("c=d", 3);
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key2, value2, cookies));
    EXPECT_EQ(cookies, "a=b; c=d");
  }
}

// For issue #1421 regression test that Envoy's H2 codec applies header limits early.
TEST_P(Http2CodecImplTest, TestCodecHeaderLimits) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(1024, 'q');
  for (int i = 0; i < 63; ++i) {
    request_headers.addCopy(fmt::format("{}", i), long_string);
  }
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_));
  request_encoder_->encodeHeaders(request_headers, false);
}

TEST_P(Http2CodecImplTest, TestCodecHeaderCompression) {
  initialize();

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestHeaderMapImpl response_headers{{":status", "200"}, {"compression", "test"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);

  // Sanity check to verify that state of encoders and decoders matches.
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(server_.session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(client_.session()));
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(client_.session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(server_.session()));

  // Verify that headers are compressed only when both client and server advertise table size > 0:
  if (client_http2settings_.hpack_table_size_ && server_http2settings_.hpack_table_size_) {
    EXPECT_NE(0, nghttp2_session_get_hd_deflate_dynamic_table_size(client_.session()));
    EXPECT_NE(0, nghttp2_session_get_hd_deflate_dynamic_table_size(server_.session()));
  } else {
    EXPECT_EQ(0, nghttp2_session_get_hd_deflate_dynamic_table_size(client_.session()));
    EXPECT_EQ(0, nghttp2_session_get_hd_deflate_dynamic_table_size(server_.session()));
  }
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
