#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/http_11_proxy/config.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"
#include "source/extensions/transport_sockets/http_11_proxy/tunnel_io_handle.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {
namespace {

constexpr absl::string_view Target = "www.foo.com:443";
constexpr absl::string_view Credential = "Basic dXNlcjpwYXNz";
constexpr absl::string_view ConnectRequest = "CONNECT www.foo.com:443 HTTP/1.1\r\n"
                                             "Host: www.foo.com:443\r\n"
                                             "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n\r\n";

// TunnelIoHandle tests.

class TunnelIoHandleTest : public testing::Test {
public:
  NiceMock<Network::MockIoHandle> io_handle_;
  TunnelIoHandle handle_{io_handle_};
};

TEST_F(TunnelIoHandleTest, ReadEmptyReturnsAgain) {
  Buffer::OwnedImpl buffer;
  auto result = handle_.read(buffer, std::nullopt);
  EXPECT_EQ(0, result.return_value_);
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());

  char data[4];
  Buffer::RawSlice slice{data, sizeof(data)};
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, handle_.readv(4, &slice, 1).err_->getErrorCode());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, handle_.recv(data, 4, 0).err_->getErrorCode());
}

TEST_F(TunnelIoHandleTest, ReadEmptyAfterEndStreamReturnsEof) {
  handle_.setReadEndStream();
  Buffer::OwnedImpl buffer;
  auto result = handle_.read(buffer, std::nullopt);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.return_value_);
}

TEST_F(TunnelIoHandleTest, Read) {
  handle_.inbound().add("hello world");
  Buffer::OwnedImpl buffer;
  auto result = handle_.read(buffer, 5);
  EXPECT_EQ(5, result.return_value_);
  EXPECT_EQ("hello", buffer.toString());
  result = handle_.read(buffer, std::nullopt);
  EXPECT_EQ(6, result.return_value_);
  EXPECT_EQ("hello world", buffer.toString());
  EXPECT_EQ(0, handle_.inbound().length());
}

TEST_F(TunnelIoHandleTest, Readv) {
  handle_.inbound().add("hello world");
  char a[3];
  char b[10];
  Buffer::RawSlice slices[2] = {{a, sizeof(a)}, {b, sizeof(b)}};
  auto result = handle_.readv(8, slices, 2);
  EXPECT_EQ(8, result.return_value_);
  EXPECT_EQ("hel", absl::string_view(a, 3));
  EXPECT_EQ("lo wo", absl::string_view(b, 5));
  EXPECT_EQ("rld", handle_.inbound().toString());
}

TEST_F(TunnelIoHandleTest, RecvPeek) {
  handle_.inbound().add("hello");
  char data[10];
  auto result = handle_.recv(data, sizeof(data), MSG_PEEK);
  EXPECT_EQ(5, result.return_value_);
  EXPECT_EQ("hello", absl::string_view(data, 5));
  EXPECT_EQ(5, handle_.inbound().length());
  result = handle_.recv(data, 2, 0);
  EXPECT_EQ(2, result.return_value_);
  EXPECT_EQ("llo", handle_.inbound().toString());
}

TEST_F(TunnelIoHandleTest, Writes) {
  Buffer::OwnedImpl buffer("abc");
  EXPECT_EQ(3, handle_.write(buffer).return_value_);
  EXPECT_EQ(0, buffer.length());
  std::string def = "def";
  Buffer::RawSlice slices[2] = {{def.data(), def.size()}, {nullptr, 0}};
  EXPECT_EQ(3, handle_.writev(slices, 2).return_value_);
  EXPECT_EQ(3, handle_.send("ghi", 3).return_value_);
  EXPECT_EQ("abcdefghi", handle_.outbound().toString());
}

TEST_F(TunnelIoHandleTest, ShutdownRejectsWrites) {
  EXPECT_FALSE(handle_.writeShutdown());
  handle_.shutdown(ENVOY_SHUT_RD);
  EXPECT_FALSE(handle_.writeShutdown());
  handle_.shutdown(ENVOY_SHUT_WR);
  EXPECT_TRUE(handle_.writeShutdown());
  Buffer::OwnedImpl buffer("abc");
  EXPECT_FALSE(handle_.write(buffer).ok());
  EXPECT_FALSE(handle_.send("a", 1).ok());
  Buffer::RawSlice slice{nullptr, 0};
  EXPECT_FALSE(handle_.writev(&slice, 1).ok());
}

TEST_F(TunnelIoHandleTest, Close) {
  EXPECT_CALL(io_handle_, isOpen()).WillRepeatedly(Return(true));
  EXPECT_TRUE(handle_.isOpen());
  // Closing the tunnel handle does not close the real handle.
  EXPECT_CALL(io_handle_, close()).Times(0);
  EXPECT_TRUE(handle_.close().ok());
  EXPECT_FALSE(handle_.isOpen());

  Buffer::OwnedImpl buffer("abc");
  char data[4];
  Buffer::RawSlice slice{data, sizeof(data)};
  EXPECT_EQ(Api::IoError::IoErrorCode::BadFd,
            handle_.read(buffer, std::nullopt).err_->getErrorCode());
  EXPECT_EQ(Api::IoError::IoErrorCode::BadFd, handle_.readv(4, &slice, 1).err_->getErrorCode());
  EXPECT_EQ(Api::IoError::IoErrorCode::BadFd, handle_.recv(data, 4, 0).err_->getErrorCode());
  EXPECT_EQ(Api::IoError::IoErrorCode::BadFd, handle_.write(buffer).err_->getErrorCode());
}

TEST_F(TunnelIoHandleTest, UnsupportedOperations) {
  Network::Address::Ipv4Instance address("127.0.0.1", 80);
  Buffer::RawSlice slice{nullptr, 0};
  EXPECT_FALSE(handle_.sendmsg(&slice, 1, 0, nullptr, address).ok());
  Network::IoHandle::RecvMsgOutput output(1, nullptr);
  EXPECT_FALSE(handle_.recvmsg(&slice, 1, 0, {}, output).ok());
  RawSliceArrays slices(1, absl::FixedArray<Buffer::RawSlice>({slice}));
  EXPECT_FALSE(handle_.recvmmsg(slices, 0, {}, output).ok());
  EXPECT_FALSE(handle_.supportsMmsg());
  EXPECT_FALSE(handle_.supportsUdpGro());
}

TEST_F(TunnelIoHandleTest, Delegation) {
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(Return(42));
  EXPECT_EQ(42, handle_.fdDoNotUse());
  EXPECT_CALL(io_handle_, setAbortiveClose());
  handle_.setAbortiveClose();
  EXPECT_CALL(io_handle_, wasConnected()).WillOnce(Return(true));
  EXPECT_TRUE(handle_.wasConnected());
  EXPECT_CALL(io_handle_, bind(_));
  handle_.bind(nullptr);
  EXPECT_CALL(io_handle_, listen(1));
  handle_.listen(1);
  EXPECT_CALL(io_handle_, accept(_, _));
  handle_.accept(nullptr, nullptr);
  EXPECT_CALL(io_handle_, connect(_));
  handle_.connect(nullptr);
  EXPECT_CALL(io_handle_, setOption(1, 2, nullptr, 0));
  handle_.setOption(1, 2, nullptr, 0);
  EXPECT_CALL(io_handle_, getOption(SOL_SOCKET, SO_ERROR, nullptr, nullptr));
  handle_.getOption(SOL_SOCKET, SO_ERROR, nullptr, nullptr);
  EXPECT_CALL(io_handle_, ioctl(1, nullptr, 0, nullptr, 0, nullptr));
  handle_.ioctl(1, nullptr, 0, nullptr, 0, nullptr);
  EXPECT_CALL(io_handle_, setBlocking(false));
  handle_.setBlocking(false);
  EXPECT_CALL(io_handle_, domain()).WillOnce(Return(AF_INET));
  EXPECT_EQ(AF_INET, handle_.domain());
  EXPECT_CALL(io_handle_, localAddress());
  (void)handle_.localAddress();
  EXPECT_CALL(io_handle_, peerAddress());
  (void)handle_.peerAddress();
  EXPECT_CALL(io_handle_, duplicate());
  handle_.duplicate();
  EXPECT_CALL(io_handle_, lastRoundTripTime());
  handle_.lastRoundTripTime();
  EXPECT_CALL(io_handle_, congestionWindowInBytes());
  handle_.congestionWindowInBytes();
  EXPECT_CALL(io_handle_, interfaceName());
  handle_.interfaceName();

  // File events are driven by the connection through the real IoHandle.
  EXPECT_CALL(io_handle_, createFileEvent_(_, _, _, _)).Times(0);
  EXPECT_CALL(io_handle_, activateFileEvents(_)).Times(0);
  EXPECT_CALL(io_handle_, enableFileEvents(_)).Times(0);
  EXPECT_CALL(io_handle_, resetFileEvents()).Times(0);
  NiceMock<Event::MockDispatcher> dispatcher;
  handle_.initializeFileEvent(
      dispatcher, [](uint32_t) { return absl::OkStatus(); }, Event::FileTriggerType::Edge, 0);
  handle_.activateFileEvents(0);
  handle_.enableFileEvents(0);
  handle_.resetFileEvents();
}

// UpstreamHttp11TunnelSocket tests.

class TunnelSocketTest : public testing::Test {
public:
  void initialize(Network::TransportSocketPtr origin = nullptr,
                  absl::string_view credential = Credential) {
    if (origin == nullptr) {
      origin = std::make_unique<Network::RawBufferSocket>();
    }
    auto proxy = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    proxy_ = proxy.get();
    EXPECT_CALL(*proxy_, setTransportSocketCallbacks(_))
        .WillOnce(Invoke([this](Network::TransportSocketCallbacks& callbacks) {
          proxy_callbacks_ = &callbacks;
        }));
    ON_CALL(*proxy_, doWrite(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& buffer, bool end_stream) {
          const uint64_t length = buffer.length();
          proxy_sent_.move(buffer);
          proxy_end_stream_ |= end_stream;
          return Network::IoResult{Network::PostIoAction::KeepOpen, length, false};
        }));
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    ON_CALL(testing::Const(callbacks_), ioHandle()).WillByDefault(ReturnRef(io_handle_));
    ON_CALL(io_handle_, isOpen()).WillByDefault(Return(true));

    socket_ = std::make_unique<UpstreamHttp11TunnelSocket>(std::move(origin), std::move(proxy),
                                                           Target, credential);
    socket_->setTransportSocketCallbacks(callbacks_);
  }

  // Makes the next proxy read return the given data.
  void proxyRead(absl::string_view data, bool end_stream = false) {
    EXPECT_CALL(*proxy_, doRead(_))
        .WillOnce(Invoke([data = std::string(data), end_stream](Buffer::Instance& buffer) {
          buffer.add(data);
          return Network::IoResult{Network::PostIoAction::KeepOpen, data.size(), end_stream};
        }));
  }

  // Sends the CONNECT request and establishes the tunnel.
  void establishTunnel() {
    socket_->onConnected();
    Buffer::OwnedImpl buffer;
    socket_->doWrite(buffer, false);
    EXPECT_EQ(ConnectRequest, proxy_sent_.toString());
    proxy_sent_.drain(proxy_sent_.length());

    proxyRead("HTTP/1.1 200 OK\r\n\r\n");
    EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
    EXPECT_CALL(callbacks_, flushWriteBuffer());
    Network::IoResult result = socket_->doRead(buffer);
    EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
    EXPECT_TRUE(socket_->tunnelEstablished());
  }

  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  NiceMock<Network::MockTransportSocket>* proxy_{};
  Network::TransportSocketCallbacks* proxy_callbacks_{};
  Buffer::OwnedImpl proxy_sent_;
  bool proxy_end_stream_{false};
  std::unique_ptr<UpstreamHttp11TunnelSocket> socket_;
};

TEST(TunnelSocketStaticTest, FormatConnectRequest) {
  EXPECT_EQ("CONNECT www.foo.com:443 HTTP/1.1\r\nHost: www.foo.com:443\r\n\r\n",
            UpstreamHttp11TunnelSocket::formatConnectRequest(Target, ""));
  EXPECT_EQ(ConnectRequest, UpstreamHttp11TunnelSocket::formatConnectRequest(Target, Credential));
}

TEST(TunnelSocketStaticTest, ConnectTargetFromOptions) {
  auto make_options = [](const std::string& hostname) {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        std::nullopt, std::make_unique<StreamInfo::FilterState::Objects>(),
        std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>(
            hostname, Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080)));
  };
  EXPECT_EQ("www.foo.com:443", UpstreamHttp11TunnelSocket::connectTarget(
                                   make_options("www.foo.com"), nullptr, false, true));
  EXPECT_EQ("www.foo.com:80", UpstreamHttp11TunnelSocket::connectTarget(make_options("www.foo.com"),
                                                                        nullptr, false, false));
  EXPECT_EQ("www.foo.com:8443", UpstreamHttp11TunnelSocket::connectTarget(
                                    make_options("www.foo.com:8443"), nullptr, false, false));
}

TEST(TunnelSocketStaticTest, ConnectTargetFromHost) {
  NiceMock<Upstream::MockHostDescription> host;
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 443);
  ON_CALL(host, address()).WillByDefault(Return(address));

  // No proxy configured for the host.
  EXPECT_EQ(std::nullopt, UpstreamHttp11TunnelSocket::connectTarget(nullptr, nullptr, true, true));
  EXPECT_EQ(std::nullopt, UpstreamHttp11TunnelSocket::connectTarget(nullptr, &host, false, true));

  // Default proxy: use the host address, or the hostname if known.
  EXPECT_EQ("1.2.3.4:443", UpstreamHttp11TunnelSocket::connectTarget(nullptr, &host, true, true));
  host.hostname_ = "www.foo.com";
  EXPECT_EQ("www.foo.com:443",
            UpstreamHttp11TunnelSocket::connectTarget(nullptr, &host, true, true));

  // Proxy address from the host or locality metadata.
  envoy::config::core::v3::Address proxy_address;
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ASSERT_TRUE(
      (*metadata->mutable_typed_filter_metadata())[Config::MetadataFilters::get()
                                                       .ENVOY_HTTP11_PROXY_TRANSPORT_SOCKET_ADDR]
          .PackFrom(proxy_address));
  EXPECT_CALL(host, localityMetadata()).WillRepeatedly(Return(metadata));
  EXPECT_EQ("www.foo.com:443",
            UpstreamHttp11TunnelSocket::connectTarget(nullptr, &host, false, true));
  EXPECT_CALL(host, metadata()).WillRepeatedly(Return(metadata));
  EXPECT_EQ("www.foo.com:443",
            UpstreamHttp11TunnelSocket::connectTarget(nullptr, &host, false, true));
}

TEST_F(TunnelSocketTest, SendsConnectRequestThroughProxySocket) {
  initialize();
  EXPECT_CALL(*proxy_, onConnected());
  socket_->onConnected();

  // Application data is held until the tunnel is established.
  Buffer::OwnedImpl data("GET / HTTP/1.1\r\n\r\n");
  Network::IoResult result = socket_->doWrite(data, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(0, result.bytes_processed_);
  EXPECT_EQ("GET / HTTP/1.1\r\n\r\n", data.toString());
  EXPECT_EQ(ConnectRequest, proxy_sent_.toString());
  EXPECT_FALSE(socket_->tunnelEstablished());
}

TEST_F(TunnelSocketTest, SendsConnectRequestWithoutCredential) {
  initialize(nullptr, "");
  Buffer::OwnedImpl data;
  socket_->doWrite(data, false);
  EXPECT_EQ("CONNECT www.foo.com:443 HTTP/1.1\r\nHost: www.foo.com:443\r\n\r\n",
            proxy_sent_.toString());
}

TEST_F(TunnelSocketTest, ConnectRequestSentAfterProxyHandshakeCompletesOnRead) {
  initialize();
  // The proxy socket does not write anything until its handshake completes.
  EXPECT_CALL(*proxy_, doWrite(_, false))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 0, false}))
      .WillRepeatedly(testing::DoDefault());
  Buffer::OwnedImpl data;
  socket_->doWrite(data, false);
  EXPECT_EQ(0, proxy_sent_.length());

  // The handshake completes during a read: the proxy Connected event is not propagated, and the
  // CONNECT request is sent.
  EXPECT_CALL(*proxy_, doRead(_)).WillOnce(Invoke([this](Buffer::Instance&) {
    EXPECT_CALL(callbacks_, raiseEvent(_)).Times(0);
    proxy_callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
    return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
  }));
  Network::IoResult result = socket_->doRead(data);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(ConnectRequest, proxy_sent_.toString());
}

TEST_F(TunnelSocketTest, TunnelEstablishedPlaintextOrigin) {
  initialize();
  establishTunnel();

  // Writes go through the tunnel.
  Buffer::OwnedImpl data("request");
  Network::IoResult result = socket_->doWrite(data, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ(7, result.bytes_processed_);
  EXPECT_EQ("request", proxy_sent_.toString());

  // Reads come from the tunnel.
  proxyRead("response");
  Buffer::OwnedImpl read_buffer;
  result = socket_->doRead(read_buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ("response", read_buffer.toString());
}

TEST_F(TunnelSocketTest, DataAfterConnectResponseIsPassedToOrigin) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nhello");
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  Network::IoResult result = socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
  EXPECT_EQ("hello", buffer.toString());
}

TEST_F(TunnelSocketTest, FragmentedConnectResponse) {
  initialize();
  socket_->onConnected();
  // The CONNECT request cannot be written yet.
  EXPECT_CALL(*proxy_, doWrite(_, false))
      .WillRepeatedly(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 0, false}));
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  // Response data received before the CONNECT request is fully written is not processed.
  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  EXPECT_CALL(callbacks_, raiseEvent(_)).Times(0);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, socket_->doRead(buffer).action_);
  EXPECT_FALSE(socket_->tunnelEstablished());
}

TEST_F(TunnelSocketTest, PartialConnectResponse) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("HTTP/1.1 200");
  EXPECT_CALL(callbacks_, raiseEvent(_)).Times(0);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, socket_->doRead(buffer).action_);
  EXPECT_FALSE(socket_->tunnelEstablished());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  proxyRead(" OK\r\n\r\n");
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_EQ(Network::PostIoAction::KeepOpen, socket_->doRead(buffer).action_);
  EXPECT_TRUE(socket_->tunnelEstablished());
}

TEST_F(TunnelSocketTest, NonSuccessConnectResponse) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("HTTP/1.1 407 Proxy Authentication Required\r\n\r\n");
  EXPECT_CALL(callbacks_, raiseEvent(_)).Times(0);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ("proxy CONNECT failed: 407", socket_->failureReason());
  // Subsequent operations fail.
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doWrite(buffer, false).action_);
}

TEST_F(TunnelSocketTest, InvalidConnectResponse) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("this is not http\r\n\r\n");
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ("invalid response to CONNECT from proxy", socket_->failureReason());
}

TEST_F(TunnelSocketTest, ConnectResponseTooLarge) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead(absl::StrCat("HTTP/1.1 200 OK\r\nfoo: ",
                         std::string(UpstreamHttp11TunnelSocket::MaxResponseHeaderSize, 'a')));
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ("invalid response to CONNECT from proxy", socket_->failureReason());
}

TEST_F(TunnelSocketTest, ProxyClosesBeforeConnectResponse) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("", true);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ("proxy closed the connection before responding to CONNECT", socket_->failureReason());
}

TEST_F(TunnelSocketTest, ProxyReadError) {
  initialize();
  EXPECT_CALL(*proxy_, doRead(_))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::Close, 0, false}));
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
}

TEST_F(TunnelSocketTest, ProxyWriteErrorBeforeTunnel) {
  initialize();
  EXPECT_CALL(*proxy_, doWrite(_, _))
      .WillRepeatedly(Return(Network::IoResult{Network::PostIoAction::Close, 0, false}));
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doWrite(buffer, false).action_);
  proxyRead("");
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
}

TEST_F(TunnelSocketTest, InvalidCredential) {
  initialize(nullptr, "Basic abc\r\nX-Injected: 1");
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*proxy_, doWrite(_, _)).Times(0);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doWrite(buffer, false).action_);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
  EXPECT_EQ("invalid proxy authorization credential", socket_->failureReason());
}

TEST_F(TunnelSocketTest, ProxyWriteErrorAfterTunnel) {
  initialize();
  establishTunnel();
  EXPECT_CALL(*proxy_, doWrite(_, _))
      .WillRepeatedly(Return(Network::IoResult{Network::PostIoAction::Close, 0, false}));
  Buffer::OwnedImpl data("request");
  Network::IoResult result = socket_->doWrite(data, false);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  EXPECT_EQ(7, result.bytes_processed_);

  // Reads also fail when the pending data cannot be flushed to the proxy.
  proxyRead("response");
  Buffer::OwnedImpl read_buffer;
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(read_buffer).action_);
}

TEST_F(TunnelSocketTest, HalfCloseIsPropagatedToProxy) {
  initialize();
  establishTunnel();
  Buffer::OwnedImpl data("last");
  socket_->doWrite(data, true);
  EXPECT_EQ("last", proxy_sent_.toString());
  EXPECT_TRUE(proxy_end_stream_);
}

TEST_F(TunnelSocketTest, EndOfStreamFromProxyIsPropagatedToOrigin) {
  initialize();
  establishTunnel();
  proxyRead("bye", true);
  Buffer::OwnedImpl buffer;
  Network::IoResult result = socket_->doRead(buffer);
  EXPECT_EQ("bye", buffer.toString());
  EXPECT_TRUE(result.end_stream_read_);

  // The proxy is not read again after end of stream.
  EXPECT_CALL(*proxy_, doRead(_)).Times(0);
  result = socket_->doRead(buffer);
  EXPECT_TRUE(result.end_stream_read_);
}

TEST_F(TunnelSocketTest, ConnectionClosedDuringConnectedEvent) {
  initialize();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([this](Network::ConnectionEvent) {
        ON_CALL(io_handle_, isOpen()).WillByDefault(Return(false));
      }));
  EXPECT_CALL(callbacks_, flushWriteBuffer()).Times(0);
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
}

class MockOriginTest : public TunnelSocketTest {
public:
  void initializeWithMockOrigin() {
    auto origin = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    origin_ = origin.get();
    EXPECT_CALL(*origin_, setTransportSocketCallbacks(_))
        .WillOnce(Invoke([this](Network::TransportSocketCallbacks& callbacks) {
          origin_callbacks_ = &callbacks;
        }));
    initialize(std::move(origin));
  }

  NiceMock<Network::MockTransportSocket>* origin_{};
  Network::TransportSocketCallbacks* origin_callbacks_{};
};

TEST_F(MockOriginTest, OriginHandshakeIsDrivenWhenTunnelIsEstablished) {
  initializeWithMockOrigin();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);
  proxy_sent_.drain(proxy_sent_.length());

  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  EXPECT_CALL(*origin_, onConnected());
  // The origin socket writes its handshake to the tunnel, which is flushed to the proxy.
  EXPECT_CALL(*origin_, doWrite(_, false)).WillOnce(Invoke([this](Buffer::Instance&, bool) {
    Buffer::OwnedImpl hello("client hello");
    origin_callbacks_->ioHandle().write(hello);
    return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
  }));
  EXPECT_CALL(callbacks_, flushWriteBuffer());
  EXPECT_CALL(*origin_, doRead(_))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 0, false}));
  EXPECT_EQ(Network::PostIoAction::KeepOpen, socket_->doRead(buffer).action_);
  EXPECT_EQ("client hello", proxy_sent_.toString());
}

TEST_F(MockOriginTest, OriginHandshakeFailureWhenTunnelIsEstablished) {
  initializeWithMockOrigin();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  EXPECT_CALL(*origin_, doWrite(_, false))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::Close, 0, false}));
  EXPECT_EQ(Network::PostIoAction::Close, socket_->doRead(buffer).action_);
}

TEST_F(MockOriginTest, LeftoverTunnelDataSchedulesRead) {
  initializeWithMockOrigin();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);

  // The origin socket does not consume the data that follows the CONNECT response.
  proxyRead("HTTP/1.1 200 OK\r\n\r\npending");
  EXPECT_CALL(*origin_, doRead(_))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());
  EXPECT_EQ(Network::PostIoAction::KeepOpen, socket_->doRead(buffer).action_);
}

TEST_F(MockOriginTest, Delegation) {
  initializeWithMockOrigin();

  EXPECT_CALL(*origin_, protocol()).WillOnce(Return("h2"));
  EXPECT_EQ("h2", socket_->protocol());
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  EXPECT_CALL(*origin_, ssl()).WillOnce(Return(ssl));
  EXPECT_EQ(ssl, socket_->ssl());
  EXPECT_FALSE(socket_->startSecureTransport());

  EXPECT_CALL(*origin_, canFlushClose()).WillOnce(Return(true));
  EXPECT_CALL(*proxy_, canFlushClose()).WillOnce(Return(false));
  EXPECT_FALSE(socket_->canFlushClose());

  NiceMock<Network::MockConnectionSocket> connection_socket;
  EXPECT_CALL(*proxy_, connect(_)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  EXPECT_CALL(*origin_, connect(_)).Times(0);
  socket_->connect(connection_socket);

  EXPECT_CALL(*proxy_, configureInitialCongestionWindow(100, std::chrono::microseconds(10)));
  socket_->configureInitialCongestionWindow(100, std::chrono::microseconds(10));

  // Failure reasons: proxy first, then origin.
  EXPECT_CALL(*proxy_, failureReason()).WillRepeatedly(Return(""));
  EXPECT_CALL(*origin_, failureReason()).WillOnce(Return("origin failure"));
  EXPECT_EQ("origin failure", socket_->failureReason());
  EXPECT_CALL(*proxy_, failureReason()).WillRepeatedly(Return("proxy failure"));
  EXPECT_EQ("proxy failure", socket_->failureReason());
}

TEST_F(MockOriginTest, CloseSocket) {
  initializeWithMockOrigin();
  {
    testing::InSequence s;
    EXPECT_CALL(*origin_, closeSocket(Network::ConnectionEvent::LocalClose, false));
    EXPECT_CALL(*proxy_, closeSocket(Network::ConnectionEvent::LocalClose, false));
  }
  // The tunnel is not established, so nothing is flushed.
  EXPECT_CALL(*proxy_, doWrite(_, _)).Times(0);
  socket_->closeSocket(Network::ConnectionEvent::LocalClose, false);
}

TEST_F(MockOriginTest, CloseSocketFlushesOriginData) {
  initializeWithMockOrigin();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);
  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  socket_->doRead(buffer);
  proxy_sent_.drain(proxy_sent_.length());

  EXPECT_CALL(*origin_, closeSocket(_, false))
      .WillOnce(Invoke([this](Network::ConnectionEvent, bool) {
        Buffer::OwnedImpl alert("close_notify");
        origin_callbacks_->ioHandle().write(alert);
        origin_callbacks_->ioHandle().shutdown(ENVOY_SHUT_WR);
      }));
  EXPECT_CALL(*proxy_, closeSocket(_, false));
  socket_->closeSocket(Network::ConnectionEvent::LocalClose, false);
  EXPECT_EQ("close_notify", proxy_sent_.toString());
  EXPECT_TRUE(proxy_end_stream_);
}

TEST_F(MockOriginTest, AbortResetDoesNotFlush) {
  initializeWithMockOrigin();
  socket_->onConnected();
  Buffer::OwnedImpl buffer;
  socket_->doWrite(buffer, false);
  proxyRead("HTTP/1.1 200 OK\r\n\r\n");
  socket_->doRead(buffer);

  EXPECT_CALL(*origin_, closeSocket(_, true));
  EXPECT_CALL(*proxy_, doWrite(_, _)).Times(0);
  EXPECT_CALL(*proxy_, closeSocket(_, true));
  socket_->closeSocket(Network::ConnectionEvent::RemoteClose, true);
}

TEST_F(MockOriginTest, ProxyCallbacks) {
  initializeWithMockOrigin();
  EXPECT_EQ(&io_handle_, &proxy_callbacks_->ioHandle());
  EXPECT_EQ(&io_handle_,
            &static_cast<const Network::TransportSocketCallbacks*>(proxy_callbacks_)->ioHandle());
  EXPECT_EQ(&callbacks_.connection_, &proxy_callbacks_->connection());

  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());
  proxy_callbacks_->setTransportSocketIsReadable();
  EXPECT_CALL(callbacks_, flushWriteBuffer());
  proxy_callbacks_->flushWriteBuffer();
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::RemoteClose));
  proxy_callbacks_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  // shouldDrainReadBuffer accounts for the data buffered in the tunnel.
  EXPECT_CALL(callbacks_, shouldDrainReadBuffer()).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.connection_, bufferLimit()).WillRepeatedly(Return(0));
  EXPECT_FALSE(proxy_callbacks_->shouldDrainReadBuffer());
  EXPECT_CALL(callbacks_.connection_, bufferLimit()).WillRepeatedly(Return(4));
  EXPECT_FALSE(proxy_callbacks_->shouldDrainReadBuffer());
  EXPECT_CALL(*proxy_, doRead(_)).WillOnce(Invoke([this](Buffer::Instance& buffer) {
    buffer.add("12345");
    EXPECT_TRUE(proxy_callbacks_->shouldDrainReadBuffer());
    return Network::IoResult{Network::PostIoAction::KeepOpen, 5, false};
  }));
  Buffer::OwnedImpl buffer;
  socket_->doRead(buffer);
}

TEST_F(MockOriginTest, TunnelCallbacks) {
  initializeWithMockOrigin();
  EXPECT_NE(&io_handle_, &origin_callbacks_->ioHandle());
  EXPECT_EQ(&origin_callbacks_->ioHandle(),
            &static_cast<const Network::TransportSocketCallbacks*>(origin_callbacks_)->ioHandle());
  EXPECT_EQ(&callbacks_.connection_, &origin_callbacks_->connection());

  EXPECT_CALL(callbacks_, shouldDrainReadBuffer()).WillOnce(Return(true));
  EXPECT_TRUE(origin_callbacks_->shouldDrainReadBuffer());
  EXPECT_CALL(callbacks_, setTransportSocketIsReadable());
  origin_callbacks_->setTransportSocketIsReadable();
  EXPECT_CALL(callbacks_, flushWriteBuffer());
  origin_callbacks_->flushWriteBuffer();
  EXPECT_CALL(callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  origin_callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
}

// Factory tests.

class TunnelSocketFactoryTest : public testing::Test {
public:
  void initialize(bool default_proxy) {
    auto inner_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
    inner_factory_ = inner_factory.get();
    auto proxy_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
    proxy_factory_ = proxy_factory.get();
    std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info;
    if (default_proxy) {
      proxy_info.emplace("", Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080));
    }
    factory_ = std::make_unique<UpstreamHttp11ConnectSocketFactory>(
        std::move(inner_factory), proxy_info, std::move(proxy_factory));
    ON_CALL(host_, address())
        .WillByDefault(Return(Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 443)));
  }

  NiceMock<Network::MockTransportSocketFactory>* inner_factory_{};
  NiceMock<Network::MockTransportSocketFactory>* proxy_factory_{};
  std::unique_ptr<UpstreamHttp11ConnectSocketFactory> factory_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_ptr_{
      std::make_shared<NiceMock<Upstream::MockHostDescription>>()};
  NiceMock<Upstream::MockHostDescription>& host_{*host_ptr_};
};

TEST_F(TunnelSocketFactoryTest, TunnelsTraffic) {
  initialize(true);
  EXPECT_TRUE(factory_->tunnelsHttp11ProxyTraffic());

  auto plain = std::make_unique<UpstreamHttp11ConnectSocketFactory>(
      std::make_unique<NiceMock<Network::MockTransportSocketFactory>>());
  EXPECT_FALSE(plain->tunnelsHttp11ProxyTraffic());
}

TEST_F(TunnelSocketFactoryTest, NotProxiedUsesInnerSocket) {
  initialize(false);
  auto inner = std::make_unique<NiceMock<Network::MockTransportSocket>>();
  Network::TransportSocket* inner_ptr = inner.get();
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _))
      .WillOnce(Return(ByMove(std::move(inner))));
  EXPECT_CALL(*proxy_factory_, createTransportSocket(_, _)).Times(0);
  EXPECT_EQ(inner_ptr, factory_->createTransportSocket(nullptr, host_ptr_).get());
}

TEST_F(TunnelSocketFactoryTest, ProxiedCreatesTunnelSocket) {
  initialize(true);
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<Network::MockTransportSocket>>())));
  // Transport socket options are not passed to the proxy socket.
  EXPECT_CALL(*proxy_factory_, createTransportSocket(testing::IsNull(), _))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<Network::MockTransportSocket>>())));
  auto socket = factory_->createTransportSocket(nullptr, host_ptr_);
  EXPECT_NE(nullptr, dynamic_cast<UpstreamHttp11TunnelSocket*>(socket.get()));
}

TEST_F(TunnelSocketFactoryTest, NullSockets) {
  initialize(true);
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _)).WillOnce(Return(ByMove(nullptr)));
  EXPECT_EQ(nullptr, factory_->createTransportSocket(nullptr, host_ptr_));

  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<Network::MockTransportSocket>>())));
  EXPECT_CALL(*proxy_factory_, createTransportSocket(_, _)).WillOnce(Return(ByMove(nullptr)));
  EXPECT_EQ(nullptr, factory_->createTransportSocket(nullptr, host_ptr_));
}

// Config tests.

class TunnelConfigTest : public testing::Test {
public:
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> create(
      const envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport&
          config) {
    return factory_.createTransportSocketFactory(config, context_);
  }

  UpstreamHttp11ConnectSocketConfigFactory factory_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

TEST_F(TunnelConfigTest, ProxyTransportSocketEnablesTunnelMode) {
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport config;
  auto* proxy_socket = config.mutable_proxy_transport_socket();
  proxy_socket->set_name("envoy.transport_sockets.raw_buffer");
  ASSERT_TRUE(proxy_socket->mutable_typed_config()->PackFrom(
      envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer()));
  auto factory_or_error = create(config);
  ASSERT_TRUE(factory_or_error.ok());
  EXPECT_TRUE(factory_or_error.value()->tunnelsHttp11ProxyTraffic());
}

TEST_F(TunnelConfigTest, NoTunnelModeByDefault) {
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport config;
  auto factory_or_error = create(config);
  ASSERT_TRUE(factory_or_error.ok());
  EXPECT_FALSE(factory_or_error.value()->tunnelsHttp11ProxyTraffic());
}

TEST_F(TunnelConfigTest, UnknownStaticSecret) {
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport config;
  config.mutable_proxy_authorization()->set_name("missing");
  auto factory_or_error = create(config);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_EQ("Unknown static secret for proxy_authorization: missing",
            factory_or_error.status().message());
}

TEST_F(TunnelConfigTest, StaticSecretIsSentInConnect) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("proxy-credential");
  secret.mutable_generic_secret()->mutable_secret()->set_inline_string(std::string(Credential));
  ASSERT_TRUE(context_.server_context_.secretManager().addStaticSecret(secret).ok());

  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport config;
  config.mutable_proxy_authorization()->set_name("proxy-credential");
  config.mutable_default_proxy_address()->mutable_socket_address()->set_address("127.0.0.1");
  config.mutable_default_proxy_address()->mutable_socket_address()->set_port_value(8080);
  auto factory_or_error = create(config);
  ASSERT_TRUE(factory_or_error.ok());
  EXPECT_TRUE(factory_or_error.value()->tunnelsHttp11ProxyTraffic());

  auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  host->hostname_ = "www.foo.com";
  ON_CALL(*host, address())
      .WillByDefault(Return(Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 443)));
  auto socket = factory_or_error.value()->createTransportSocket(nullptr, host);
  ASSERT_NE(nullptr, socket);

  // Both the origin and proxy sockets are raw_buffer sockets: verify the CONNECT request written
  // to the real IoHandle.
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  NiceMock<Network::MockIoHandle> io_handle;
  ON_CALL(callbacks, ioHandle()).WillByDefault(ReturnRef(io_handle));
  std::string written;
  EXPECT_CALL(io_handle, write(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    written = buffer.toString();
    const uint64_t length = buffer.length();
    buffer.drain(length);
    return Api::IoCallUint64Result(length, Api::IoError::none());
  }));
  socket->setTransportSocketCallbacks(callbacks);
  EXPECT_CALL(callbacks, raiseEvent(_)).Times(0);
  socket->onConnected();
  Buffer::OwnedImpl buffer;
  socket->doWrite(buffer, false);
  EXPECT_EQ(ConnectRequest, written);
}

} // namespace
} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
