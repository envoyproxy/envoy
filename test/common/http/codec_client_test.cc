#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/exception.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtMost;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Pointee;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
namespace {

class CodecClientTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  void initialize() {
    connection_ = new NiceMock<Network::MockClientConnection>();

    EXPECT_CALL(*connection_, connecting()).WillOnce(Return(true));
    EXPECT_CALL(*connection_, detectEarlyCloseWhenReadDisabled(false));
    EXPECT_CALL(*connection_, addConnectionCallbacks(_)).WillOnce(SaveArgAddress(&connection_cb_));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, addReadFilter(_))
        .WillOnce(
            Invoke([this](Network::ReadFilterSharedPtr filter) -> void { filter_ = filter; }));

    codec_ = new Http::MockClientConnection();
    EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http11));

    Network::ClientConnectionPtr connection{connection_};
    EXPECT_CALL(dispatcher_, createTimer_(_));
    client_ = std::make_unique<CodecClientForTest>(CodecType::HTTP1, std::move(connection), codec_,
                                                   nullptr, host_, dispatcher_);
    ON_CALL(*connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  }

  ~CodecClientTest() override { EXPECT_EQ(0U, client_->numActiveRequests()); }

  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockDispatcher dispatcher_;
  Network::MockClientConnection* connection_;
  Http::MockClientConnection* codec_;
  std::unique_ptr<CodecClientForTest> client_;
  Network::ConnectionCallbacks* connection_cb_;
  Network::ReadFilterSharedPtr filter_;
  std::shared_ptr<Upstream::MockIdleTimeEnabledClusterInfo> cluster_{
      new NiceMock<Upstream::MockIdleTimeEnabledClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_{
      Upstream::makeTestHostDescription(cluster_, "tcp://127.0.0.1:80", simTime())};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(CodecClientTest, NotCallDetectEarlyCloseWhenReadDiabledUsingHttp3) {
  auto connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  EXPECT_CALL(*connection, connecting()).WillOnce(Return(true));
  EXPECT_CALL(*connection, detectEarlyCloseWhenReadDisabled(false)).Times(0);
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).WillOnce(SaveArgAddress(&connection_cb_));
  EXPECT_CALL(*connection, connect());
  EXPECT_CALL(*connection, addReadFilter(_));
  auto codec = new Http::MockClientConnection();

  EXPECT_CALL(dispatcher_, createTimer_(_));
  client_ = std::make_unique<CodecClientForTest>(CodecType::HTTP3, std::move(connection), codec,
                                                 nullptr, host_, dispatcher_);
}

TEST_F(CodecClientTest, BasicHeaderOnlyResponse) {
  initialize();

  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::RequestEncoder& request_encoder = client_->newStream(outer_decoder);

  TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_OK(request_encoder.encodeHeaders(request_headers, true));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(outer_decoder, decodeHeaders_(Pointee(Ref(*response_headers)), true));
  inner_decoder->decodeHeaders(std::move(response_headers), true);
}

TEST_F(CodecClientTest, BasicResponseWithBody) {
  initialize();
  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::RequestEncoder& request_encoder = client_->newStream(outer_decoder);

  TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_OK(request_encoder.encodeHeaders(request_headers, true));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(outer_decoder, decodeHeaders_(Pointee(Ref(*response_headers)), false));
  inner_decoder->decodeHeaders(std::move(response_headers), false);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(outer_decoder, decodeData(Ref(buffer), true));
  inner_decoder->decodeData(buffer, true);
}

TEST_F(CodecClientTest, DisconnectBeforeHeaders) {
  initialize();
  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::StreamEncoder& request_encoder = client_->newStream(outer_decoder);
  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);

  // When we get a remote close with an active request we should try to send zero bytes through
  // the codec.
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::ConnectionTermination, _));
  EXPECT_CALL(*codec_, dispatch(_));
  connection_cb_->onEvent(Network::ConnectionEvent::Connected);
  connection_cb_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(CodecClientTest, IdleTimerWithNoActiveRequests) {
  initialize();
  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::RequestEncoder& request_encoder = client_->newStream(outer_decoder);
  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  connection_cb_->onEvent(Network::ConnectionEvent::Connected);

  TestRequestHeaderMapImpl request_headers{
      {":authority", "host"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_OK(request_encoder.encodeHeaders(request_headers, true));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(outer_decoder, decodeHeaders_(Pointee(Ref(*response_headers)), false));
  inner_decoder->decodeHeaders(std::move(response_headers), false);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(outer_decoder, decodeData(Ref(buffer), true));
  inner_decoder->decodeData(buffer, true);
  EXPECT_NE(client_->idleTimer(), nullptr);

  // Close the client and validate idleTimer is reset
  EXPECT_EQ(client_->numActiveRequests(), 0);
  client_->close();
  // TODO(ramaraochavali): Use default connection mock handlers for raising events.
  connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(client_->idleTimer(), nullptr);
}

TEST_F(CodecClientTest, IdleTimerClientRemoteCloseWithActiveRequests) {
  initialize();
  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::StreamEncoder& request_encoder = client_->newStream(outer_decoder);
  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);

  // When we get a remote close with an active request validate idleTimer is reset after client
  // close
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::ConnectionTermination, _));
  EXPECT_CALL(*codec_, dispatch(_));
  EXPECT_NE(client_->numActiveRequests(), 0);
  connection_cb_->onEvent(Network::ConnectionEvent::Connected);
  connection_cb_->onEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(client_->idleTimer(), nullptr);
}

TEST_F(CodecClientTest, IdleTimerClientLocalCloseWithActiveRequests) {
  initialize();
  ResponseDecoder* inner_decoder;
  NiceMock<MockRequestEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockResponseDecoder outer_decoder;
  Http::StreamEncoder& request_encoder = client_->newStream(outer_decoder);
  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);

  // When we get a local close with an active request validate idleTimer is reset after client close
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::ConnectionTermination, _));
  connection_cb_->onEvent(Network::ConnectionEvent::Connected);
  // TODO(ramaraochavali): Use default connection mock handlers for raising events.
  client_->close();
  connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(client_->idleTimer(), nullptr);
}

TEST_F(CodecClientTest, ProtocolError) {
  initialize();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Return(codecProtocolError("protocol error")));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data, false);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, 408Response) {
  initialize();
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Return(prematureResponseError("", Code::RequestTimeout)));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data, false);

  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, PrematureResponse) {
  initialize();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Return(prematureResponseError("", Code::OK)));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data, false);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, WatermarkPassthrough) {
  initialize();
  EXPECT_CALL(*codec_, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  connection_cb_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(*codec_, onUnderlyingConnectionBelowWriteBufferLowWatermark());
  connection_cb_->onBelowWriteBufferLowWatermark();
}

// Test the codec getting input from a real TCP connection.
class CodecNetworkTest : public Event::TestUsingSimulatedTime,
                         public testing::TestWithParam<Network::Address::IpVersion> {
public:
  CodecNetworkTest() : api_(Api::createApiForTest()), stream_info_(api_->timeSource(), nullptr) {
    dispatcher_ = api_->allocateDispatcher("test_thread");
    auto socket = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(GetParam()));
    Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
        socket->connectionInfoProvider().localAddress(), source_address_,
        Network::Test::createRawBufferSocket(), nullptr, nullptr);
    upstream_listener_ =
        dispatcher_->createListener(std::move(socket), listener_callbacks_, runtime_, true, false);
    client_connection_ = client_connection.get();
    client_connection_->addConnectionCallbacks(client_callbacks_);

    codec_ = new Http::MockClientConnection();
    EXPECT_CALL(*codec_, protocol()).WillRepeatedly(Return(Protocol::Http11));
    client_ = std::make_unique<CodecClientForTest>(CodecType::HTTP1, std::move(client_connection),
                                                   codec_, nullptr, host_, *dispatcher_);

    int expected_callbacks = 2;
    EXPECT_CALL(listener_callbacks_, onAccept_(_))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
          upstream_connection_ = dispatcher_->createServerConnection(
              std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
          upstream_connection_->addConnectionCallbacks(upstream_callbacks_);

          expected_callbacks--;
          if (expected_callbacks == 0) {
            dispatcher_->exit();
          }
        }));

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(InvokeWithoutArgs([&]() -> void {
          expected_callbacks--;
          if (expected_callbacks == 0) {
            dispatcher_->exit();
          }
        }));

    // Since we mocked the connected event, we need to mock these close events even though we don't
    // care about them in these tests.
    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose)).Times(AtMost(1));
    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::LocalClose)).Times(AtMost(1));

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void createNewStream() {
    ResponseDecoder* inner_decoder;
    EXPECT_CALL(*codec_, newStream(_))
        .WillOnce(Invoke([&](ResponseDecoder& decoder) -> RequestEncoder& {
          inner_decoder = &decoder;
          return inner_encoder_;
        }));

    client_->newStream(outer_decoder_);
  }

  void close() {
    client_->close();
    EXPECT_CALL(upstream_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

protected:
  Api::ApiPtr api_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::DispatcherPtr dispatcher_;
  Network::ListenerPtr upstream_listener_;
  Network::MockTcpListenerCallbacks listener_callbacks_;
  Network::Address::InstanceConstSharedPtr source_address_;
  Http::MockClientConnection* codec_;
  std::unique_ptr<CodecClientForTest> client_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_{
      Upstream::makeTestHostDescription(cluster_, "tcp://127.0.0.1:80", simTime())};
  Network::ConnectionPtr upstream_connection_;
  NiceMock<Network::MockConnectionCallbacks> upstream_callbacks_;
  Network::ClientConnection* client_connection_{};
  NiceMock<Network::MockConnectionCallbacks> client_callbacks_;
  NiceMock<MockRequestEncoder> inner_encoder_;
  NiceMock<MockResponseDecoder> outer_decoder_;
  StreamInfo::StreamInfoImpl stream_info_;
};

// Send a block of data from upstream, and ensure it is received by the codec.
TEST_P(CodecNetworkTest, SendData) {
  createNewStream();

  const std::string full_data = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n";
  Buffer::OwnedImpl data(full_data);
  upstream_connection_->write(data, false);
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    EXPECT_EQ(full_data, data.toString());
    data.drain(data.length());
    dispatcher_->exit();
    return Http::okStatus();
  }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(inner_encoder_.stream_, resetStream(_));
  close();
}

// Send a block of data, and then have upstream close the connection.
// Make sure that the data is passed on as is the network event.
TEST_P(CodecNetworkTest, SendHeadersAndClose) {
  createNewStream();

  // Send some header data.
  const std::string full_data = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n";
  Buffer::OwnedImpl data(full_data);
  upstream_connection_->write(data, false);
  upstream_connection_->close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
        EXPECT_EQ(full_data, data.toString());
        data.drain(data.length());
        return Http::okStatus();
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
        EXPECT_EQ("", data.toString());
        data.drain(data.length());
        return Http::okStatus();
      }));
  // Because the headers are not complete, the disconnect will reset the stream.
  // Note even if the final \r\n were appended to the header data, enough of the
  // codec state is mocked out that the data would not be framed and the stream
  // would not be finished.
  EXPECT_CALL(inner_encoder_.stream_, resetStream(_)).WillOnce(InvokeWithoutArgs([&]() -> void {
    for (auto callbacks : inner_encoder_.stream_.callbacks_) {
      callbacks->onResetStream(StreamResetReason::RemoteReset, absl::string_view());
    }
    dispatcher_->exit();
  }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Mark the stream read disabled, then send a block of data and close the connection. Ensure the
// data is drained before the connection close is processed.
// Regression test for https://github.com/envoyproxy/envoy/issues/1679
TEST_P(CodecNetworkTest, SendHeadersAndCloseUnderReadDisable) {
  createNewStream();

  client_connection_->readDisable(true);
  const std::string full_data = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
  Buffer::OwnedImpl data(full_data);
  upstream_connection_->write(data, false);
  upstream_connection_->close(Network::ConnectionCloseType::FlushWrite);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  client_connection_->readDisable(false);

  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
        EXPECT_EQ(full_data, data.toString());
        data.drain(data.length());
        return Http::okStatus();
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
        EXPECT_EQ("", data.toString());
        return Http::okStatus();
      }));
  EXPECT_CALL(inner_encoder_.stream_, resetStream(_)).WillOnce(InvokeWithoutArgs([&]() -> void {
    for (auto callbacks : inner_encoder_.stream_.callbacks_) {
      callbacks->onResetStream(StreamResetReason::RemoteReset, absl::string_view());
    }
    dispatcher_->exit();
  }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, CodecNetworkTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Http
} // namespace Envoy
