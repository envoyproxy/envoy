#include "envoy/api/io_error.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.validate.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.validate.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/http_11_proxy/config.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::Const;
using testing::InSequence;
using testing::NiceMock;
using testing::Optional;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {
namespace {

class Http11ConnectTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  Http11ConnectTest() = default;
  void initialize(bool no_proxy_protocol = false) {
    std::string url = Network::Test::getLoopbackAddressUrlString(GetParam());
    uint16_t port = 1234;
    std::string address_string = fmt::format("{}:{}", url, port);
    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::parseInternetAddressAndPortNoThrow(address_string);

    const std::string hostname = "www.foo.com";

    auto info =
        std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>(hostname, address);
    if (no_proxy_protocol) {
      info.reset();
    }
    initializeOptions(std::move(info));
    setAddress();
    if (info != nullptr) {
      initializeSockets(hostname, info->proxy_address->asString());
    } else {
      initializeSockets(hostname, absl::nullopt);
    }
  }

  Network::TransportSocketOptionsConstSharedPtr options_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<UpstreamHttp11ConnectSocket> connect_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  Buffer::OwnedImpl connect_data_{"CONNECT www.foo.com:443 HTTP/1.1\r\n\r\n"};
  Ssl::ConnectionInfoConstSharedPtr ssl_{
      std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>()};

protected:
  using ProxyInfoPtr = std::unique_ptr<Network::TransportSocketOptions::Http11ProxyInfo>;

  void initializeOptions(ProxyInfoPtr proxy_info) {
    options_ = std::make_shared<const Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, nullptr, std::move(proxy_info));
  }

  void setAddress() {
    std::string address_string =
        absl::StrCat(Network::Test::getLoopbackAddressUrlString(GetParam()), ":1234");
    auto address = Network::Utility::parseInternetAddressAndPortNoThrow(address_string);
    transport_callbacks_.connection_.stream_info_.filterState()->setData(
        "envoy.network.transport_socket.http_11_proxy.address",
        std::make_unique<Network::Http11ProxyInfoFilterState>("www.foo.com", address),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  void initializeSockets(std::string hostname, absl::optional<std::string> proxy_address) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    EXPECT_CALL(*inner_socket_, ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(Const(*inner_socket_), ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));

    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));

    using MockHost = NiceMock<Upstream::MockHostDescription>;
    auto host = std::make_shared<MockHost>();
    auto& hostref = *host;
    EXPECT_CALL(hostref, hostname()).Times(AnyNumber()).WillRepeatedly(ReturnRef(hostname));

    connect_socket_ = std::make_unique<UpstreamHttp11ConnectSocket>(
        std::move(inner_socket), options_, host, !proxy_address.has_value());
    connect_socket_->setTransportSocketCallbacks(transport_callbacks_);
    connect_socket_->onConnected();
  }
};

// Test proxy address configuration via filter state exhibits expected behavior.
TEST_P(Http11ConnectTest, ProxyAddressConfigBehaviorLegacy) {
  // Hostname of the upstream that the CONNECT proxy would send to.
  const std::string hostname = "www.foo.com";

  // Build a proxy info struct with an address specific to filter state. This is what
  // would be populated if some intermediate filter added the metadata to the
  // stream info.
  const std::string fs_proxy_address_str = "filterstate.proxy.address";
  Network::Address::InstanceConstSharedPtr fs_proxy_address =
      Network::Utility::parseInternetAddressAndPortNoThrow(fs_proxy_address_str);
  auto http11_proxy_info = std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>(
      hostname, fs_proxy_address);

  initializeOptions(std::move(http11_proxy_info));
  initializeSockets(hostname, absl::nullopt);

  // We expect just the connect header to be written by the outer socket, since everything else will
  // get buffered.
  std::string expected_connect_data = fmt::format("CONNECT {}:443 HTTP/1.1\r\n\r\n", hostname);
  std::string written_data;
  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_connect_data)))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) {
        written_data = buffer.toString();
        auto length = buffer.length();
        buffer.drain(length);
        return Api::IoCallUint64Result(length, Api::IoError::none());
      }));

  Buffer::OwnedImpl msg("blah");
  auto result = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(absl::nullopt, result.err_code_);
  EXPECT_CALL(*inner_socket_, onConnected());
  connect_socket_->onConnected();
}

// Test proxy address configuration via protobuf.
TEST_P(Http11ConnectTest, ProxyAddressConfigBehaviorProtos) {
  const std::string proxy_url = "proxyurl.biz.cx.lol";
  constexpr uint16_t proxy_port = 1337;
  const std::string proxy_addr = fmt::format("{}:{}", proxy_url, proxy_port);

  // We don't pass a proxy info struct here. Those are created only when there is filter state
  // information to populate it from and we get our proxy information from the outer transport
  // socket protobuf config.
  initializeOptions(nullptr);

  // Let's manually initialize the sockets for this test, since we need to make an actual host
  // description.
  auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
  inner_socket_ = inner_socket.get();
  EXPECT_CALL(*inner_socket_, ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));
  EXPECT_CALL(Const(*inner_socket_), ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));
  ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));

  // Represents the target host that will appear inside the CONNECT request.
  const std::string hostip = "3.3.3.2";
  constexpr uint16_t hostport = 11111;
  auto host = std::make_shared<Upstream::MockHostDescription>();
  auto hostaddr = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(hostip, hostport)};
  EXPECT_CALL(*host, address()).Times(AnyNumber()).WillRepeatedly(Return(hostaddr));
  connect_socket_ = std::make_unique<UpstreamHttp11ConnectSocket>(
      std::move(inner_socket), options_, host, false /* legacy behavior */);
  connect_socket_->setTransportSocketCallbacks(transport_callbacks_);
  connect_socket_->onConnected();

  // We expect just the connect header to be written by the outer socket, since everything else will
  // get buffered. Note the IP/port combo in this header is the target host that we expect the proxy
  // to open a CONNECT tunnel to.
  std::string expected_connect_data =
      fmt::format("CONNECT {}:{} HTTP/1.1\r\n\r\n", hostip, hostport);
  std::string written_data;
  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_connect_data)))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) {
        written_data = buffer.toString();
        auto length = buffer.length();
        buffer.drain(length);
        return Api::IoCallUint64Result(length, Api::IoError::none());
      }));

  Buffer::OwnedImpl msg("blah");
  auto result = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(absl::nullopt, result.err_code_);
  EXPECT_CALL(*inner_socket_, onConnected());
  connect_socket_->onConnected();
}

// Test injects CONNECT only once
TEST_P(Http11ConnectTest, InjectsHeaderOnlyOnce) {
  initialize();

  EXPECT_CALL(io_handle_, write(BufferStringEqual(connect_data_.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) {
        auto length = buffer.length();
        buffer.drain(length);
        return Api::IoCallUint64Result(length, Api::IoError::none());
      }));
  Buffer::OwnedImpl msg("initial data");

  Network::IoResult rc1 = connect_socket_->doWrite(msg, false);
  // Only the connect will be written initially. All other writes should be
  // buffered in the Network::Connection buffer until the connect has been
  // processed.
  EXPECT_EQ(connect_data_.length(), rc1.bytes_processed_);
  Network::IoResult rc2 = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(0, rc2.bytes_processed_);

  EXPECT_CALL(*inner_socket_, onConnected());
  connect_socket_->onConnected();
}

// Test the socket is a no-op if there's no header proto.
TEST_P(Http11ConnectTest, NoInjectHeaderProtoAbsent) {
  initialize(true);

  EXPECT_CALL(io_handle_, write(_)).Times(0);
  Buffer::OwnedImpl msg("initial data");
  Buffer::OwnedImpl msg2("new data");

  {
    InSequence s;
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg.length(), false}));
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg2), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg2.length(), false}));
  }

  Network::IoResult rc1 = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(msg.length(), rc1.bytes_processed_);
  Network::IoResult rc2 = connect_socket_->doWrite(msg2, false);
  EXPECT_EQ(msg2.length(), rc2.bytes_processed_);

  EXPECT_CALL(*inner_socket_, onConnected());
  connect_socket_->onConnected();

  // Make sure the response path is a no-op as well.
  EXPECT_CALL(io_handle_, recv(_, _, _)).Times(0);
  EXPECT_CALL(io_handle_, read(_, _)).Times(0);
  Buffer::OwnedImpl buffer("");
  connect_socket_->doRead(buffer);
}

TEST_P(Http11ConnectTest, NoInjectTlsAbsent) {
  ssl_.reset();
  initialize(false);

  EXPECT_CALL(io_handle_, write(_)).Times(0);
  Buffer::OwnedImpl msg("initial data");
  Buffer::OwnedImpl msg2("new data");

  {
    InSequence s;
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg.length(), false}));
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg2), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg2.length(), false}));
  }

  Network::IoResult rc1 = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(msg.length(), rc1.bytes_processed_);
  Network::IoResult rc2 = connect_socket_->doWrite(msg2, false);
  EXPECT_EQ(msg2.length(), rc2.bytes_processed_);

  EXPECT_CALL(*inner_socket_, onConnected());
  connect_socket_->onConnected();

  // Make sure the response path is a no-op as well.
  EXPECT_CALL(io_handle_, recv(_, _, _)).Times(0);
  EXPECT_CALL(io_handle_, read(_, _)).Times(0);
  Buffer::OwnedImpl buffer("");
  connect_socket_->doRead(buffer);
}

// Test returns KeepOpen action when write error is EAGAIN
TEST_P(Http11ConnectTest, ReturnsKeepOpenWhenWriteErrorIsAgain) {
  initialize();

  Buffer::OwnedImpl msg("initial data");
  {
    InSequence s;
    EXPECT_CALL(io_handle_, write(BufferStringEqual(connect_data_.toString())))
        .WillOnce(Invoke([&](Buffer::Instance&) {
          return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
        }));
    EXPECT_CALL(io_handle_, write(BufferStringEqual(connect_data_.toString())))
        .WillOnce(Invoke([&](Buffer::Instance& buffer) {
          auto length = buffer.length();
          buffer.drain(length);
          return Api::IoCallUint64Result(length, Api::IoError::none());
        }));
  }

  Network::IoResult rc = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, rc.action_);
  rc = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, rc.action_);
}

// Test returns Close action when write error is not EAGAIN
TEST_P(Http11ConnectTest, ReturnsCloseWhenWriteErrorIsNotAgain) {
  initialize();

  Buffer::OwnedImpl msg("initial data");
  {
    InSequence s;
    EXPECT_CALL(io_handle_, write(_)).WillOnce(Invoke([&](Buffer::Instance&) {
      return Api::IoCallUint64Result(0, Network::IoSocketError::create(EADDRNOTAVAIL));
    }));
  }

  Network::IoResult rc = connect_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::Close, rc.action_);
}

// Test stripping the header.
TEST_P(Http11ConnectTest, StipsHeaderOnce) {
  initialize();

  std::string connect("HTTP/1.1 200 OK\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(), Api::IoError::none());
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, absl::optional<uint64_t>) {
        buffer.add(connect);
        return Api::IoCallUint64Result(connect.length(), Api::IoError::none());
      }));
  EXPECT_CALL(*inner_socket_, doRead(_))
      .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, 1, false}));
  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
}

TEST_P(Http11ConnectTest, InsufficientData) {
  initialize();

  std::string connect("HTTP/1.1 200 OK\r\n\r");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(), Api::IoError::none());
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, result.action_);
}

TEST_P(Http11ConnectTest, PeekFail) {
  initialize();

  std::string connect("HTTP/1.1 200 OK\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Return(
          ByMove(Api::IoCallUint64Result({}, Network::IoSocketError::create(EADDRNOTAVAIL)))));
  EXPECT_CALL(io_handle_, read(_, _)).Times(0);
  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

// Test read fail after successful peek
TEST_P(Http11ConnectTest, ReadFail) {
  initialize();

  std::string connect("HTTP/1.1 200 OK\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(), Api::IoError::none());
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          connect.length(), Network::IoSocketError::create(EADDRNOTAVAIL)))));
  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

// Test short read after successful peek.
TEST_P(Http11ConnectTest, ShortRead) {
  initialize();

  std::string connect("HTTP/1.1 200 OK\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(), Api::IoError::none());
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(
          Return(ByMove(Api::IoCallUint64Result(connect.length() - 1, Api::IoError::none()))));
  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

// If headers exceed 2000 bytes, read fails.
TEST_P(Http11ConnectTest, LongHeaders) {
  initialize();

  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK)).WillOnce(Invoke([](void* buffer, size_t, int) {
    memset(buffer, 0, 2000);
    return Api::IoCallUint64Result(2000, Api::IoError::none());
  }));
  EXPECT_CALL(io_handle_, read(_, _)).Times(0);
  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  EXPECT_LOG_CONTAINS("trace", "failed to receive CONNECT headers within 2000 bytes", {
    auto result = connect_socket_->doRead(buffer);
    EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  });
}

// If response is not 200 OK, read fails.
TEST_P(Http11ConnectTest, InvalidResponse) {
  initialize();

  std::string connect("HTTP/1.1 404 Not Found\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(), Api::IoError::none());
      }));

  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  EXPECT_LOG_CONTAINS("trace", "Response does not appear to be a successful CONNECT upgrade", {
    auto result = connect_socket_->doRead(buffer);
    EXPECT_EQ(Network::PostIoAction::Close, result.action_);
  });
}

INSTANTIATE_TEST_SUITE_P(IpVersions, Http11ConnectTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

class SocketFactoryTest : public testing::Test {
public:
  void initialize() {
    auto inner_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
    inner_factory_ = inner_factory.get();
    factory_ = std::make_unique<UpstreamHttp11ConnectSocketFactory>(std::move(inner_factory));
  }

  NiceMock<Network::MockTransportSocketFactory>* inner_factory_;
  std::unique_ptr<UpstreamHttp11ConnectSocketFactory> factory_;
};

TEST_F(SocketFactoryTest, MakeSocketWithProtoProxyAddr) {
  std::string proxy_addr = "1.2.3.4:5678";

  auto inner_factory = std::make_unique<Network::MockTransportSocketFactory>();
  EXPECT_CALL(*inner_factory, createTransportSocket(_, _))
      .WillRepeatedly(Invoke([&](Network::TransportSocketOptionsConstSharedPtr,
                                 std::shared_ptr<const Upstream::HostDescription>) {
        auto mts = std::make_unique<Network::MockTransportSocket>();
        testing::Mock::AllowLeak(mts.get());
        return mts;
      }));

  auto factory =
      std::make_unique<UpstreamHttp11ConnectSocketFactory>(std::move(inner_factory), proxy_addr);
  auto hd = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto tso = std::make_shared<Network::TransportSocketOptionsImpl>();

  auto tsptr = factory->createTransportSocket(tso, hd);
  EXPECT_THAT(tsptr.get(), testing::NotNull());
  UpstreamHttp11ConnectSocket* ts = static_cast<UpstreamHttp11ConnectSocket*>(tsptr.get());
  EXPECT_THAT(ts, testing::NotNull());

  // We never set any filter state metadata, so check if the factory gets the proxy address and that
  // it causes us to not use legacy behavior.
  EXPECT_TRUE(factory->proxyAddress().has_value());
  EXPECT_EQ(proxy_addr, factory->proxyAddress().value());
  EXPECT_FALSE(ts->legacyBehavior());
}

// Test createTransportSocket returns nullptr if inner call returns nullptr
TEST_F(SocketFactoryTest, CreateSocketReturnsNullWhenInnerFactoryReturnsNull) {
  initialize();
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _)).WillOnce(testing::ReturnNull());
  ASSERT_EQ(nullptr, factory_->createTransportSocket(nullptr, nullptr));
}

TEST(ParseTest, TestValidResponse) {
  size_t bytes_processed;
  bool headers_complete;
  {
    std::string response("HTTP/1.0 200 OK\r\n\r\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed);
  }
  {
    std::string response("HTTP/1.0 200 OK\n\r\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed);
  }
  {
    std::string response("HTTP/1.0 200 OK\n\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed);
  }
  {
    std::string response("HTTP/1.0 200 OK\r\n\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed);
  }
  {
    // Extra headers are OK.
    std::string response("HTTP/1.0 200 OK\r\nFoo: Bar\r\n\r\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed);
  }
  {
    // Extra whitespace and extra payload are OK.
    std::string response("HTTP/1.1   200  OK \r\n\r\nasdf");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                    bytes_processed));
    EXPECT_EQ(response.length(), bytes_processed + 4);
  }
  {
    // 300 is not OK.
    std::string response("HTTP/1.0 300 OK\r\n\r\n");
    ASSERT_FALSE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                     bytes_processed));
    EXPECT_TRUE(headers_complete);
  }
  {
    // Only one CRLF: incomplete headers.
    std::string response("HTTP/1.0 200 OK\r\n");
    ASSERT_FALSE(UpstreamHttp11ConnectSocket::isValidConnectResponse(response, headers_complete,
                                                                     bytes_processed));
    EXPECT_FALSE(headers_complete);
  }
}

// The SelfContainedParser is only intended for header parsing but for coverage,
// test a request with a body.
TEST(ParseTest, CoverResponseBodyHttp10) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_balsa_delay_reset", "true"}});

  std::string headers = "HTTP/1.0 200 OK\r\ncontent-length: 2\r\n\r\n";
  std::string body = "ab";

  SelfContainedParser parser;
  parser.parser().execute(headers.c_str(), headers.length());
  EXPECT_TRUE(parser.headersComplete());
  parser.parser().execute(body.c_str(), body.length());

  EXPECT_NE(parser.parser().getStatus(), Http::Http1::ParserStatus::Error);
  EXPECT_EQ(parser.parser().statusCode(), Http::Code::OK);
  EXPECT_FALSE(parser.parser().isHttp11());
  EXPECT_THAT(parser.parser().contentLength(), Optional(2));
  EXPECT_FALSE(parser.parser().isChunked());
  EXPECT_FALSE(parser.parser().hasTransferEncoding());
  EXPECT_EQ(Http::Http1::CallbackResult::Success, parser.onUrl(nullptr, 0));
  parser.onChunkHeader(false);
}

TEST(ParseTest, CoverResponseBodyHttp11) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_balsa_delay_reset", "true"}});

  std::string headers = "HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\n";
  std::string body = "ab";

  SelfContainedParser parser;
  parser.parser().execute(headers.c_str(), headers.length());
  EXPECT_TRUE(parser.headersComplete());
  parser.parser().execute(body.c_str(), body.length());

  EXPECT_NE(parser.parser().getStatus(), Http::Http1::ParserStatus::Error);
  EXPECT_EQ(parser.parser().statusCode(), Http::Code::OK);
  EXPECT_TRUE(parser.parser().isHttp11());
  EXPECT_THAT(parser.parser().contentLength(), Optional(2));
  EXPECT_FALSE(parser.parser().isChunked());
  EXPECT_FALSE(parser.parser().hasTransferEncoding());
}

// Regression tests for #34096.
TEST(ParseTest, ContentLengthZeroHttp10) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_balsa_delay_reset", "true"}});

  constexpr absl::string_view headers = "HTTP/1.0 200 OK\r\ncontent-length: 0\r\n\r\n";

  SelfContainedParser parser;
  parser.parser().execute(headers.data(), headers.length());
  EXPECT_TRUE(parser.headersComplete());

  EXPECT_NE(parser.parser().getStatus(), Http::Http1::ParserStatus::Error);
  EXPECT_EQ(parser.parser().statusCode(), Http::Code::OK);
  EXPECT_FALSE(parser.parser().isHttp11());
  EXPECT_THAT(parser.parser().contentLength(), Optional(0));
  EXPECT_FALSE(parser.parser().isChunked());
  EXPECT_FALSE(parser.parser().hasTransferEncoding());
}

TEST(ParseTest, ContentLengthZeroHttp11) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_balsa_delay_reset", "true"}});

  constexpr absl::string_view headers = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";

  SelfContainedParser parser;
  parser.parser().execute(headers.data(), headers.length());
  EXPECT_TRUE(parser.headersComplete());

  EXPECT_NE(parser.parser().getStatus(), Http::Http1::ParserStatus::Error);
  EXPECT_EQ(parser.parser().statusCode(), Http::Code::OK);
  EXPECT_TRUE(parser.parser().isHttp11());
  EXPECT_THAT(parser.parser().contentLength(), Optional(0));
  EXPECT_FALSE(parser.parser().isChunked());
  EXPECT_FALSE(parser.parser().hasTransferEncoding());
}

class SocketConfigFactoryTest : public testing::Test {};

TEST_F(SocketConfigFactoryTest, VerifyConfigPropagatesProxyAddr) {
  std::shared_ptr<UpstreamHttp11ConnectSocketConfigFactory> scf =
      std::make_shared<UpstreamHttp11ConnectSocketConfigFactory>();
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;

  const std::string addr = "1.1.1.1";
  const uint32_t port = 1234;
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport cfg;
  cfg.mutable_proxy_address()->set_port_value(port);
  cfg.mutable_proxy_address()->set_address(addr);

  auto inner_socket = cfg.mutable_transport_socket();
  envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
  inner_socket->set_name("raw");
  inner_socket->mutable_typed_config()->PackFrom(raw_buffer);

  auto factory_result = scf->createTransportSocketFactory(cfg, factory_context);
  EXPECT_TRUE(factory_result.ok());
  auto factory = std::move(factory_result.value());

  using MockHost = NiceMock<Upstream::MockHostDescription>;
  auto host = std::make_shared<MockHost>();
  auto tso = std::make_shared<Network::TransportSocketOptionsImpl>();
  auto ts = factory->createTransportSocket(tso, host);
  auto sock = static_cast<UpstreamHttp11ConnectSocket*>(ts.get());

  EXPECT_FALSE(sock->legacyBehavior());
}

} // namespace
} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
