#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::Const;
using testing::InSequence;
using testing::NiceMock;
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
    std::string address_string =
        absl::StrCat(Network::Test::getLoopbackAddressUrlString(GetParam()), ":1234");
    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::parseInternetAddressAndPort(address_string);
    auto info =
        std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>("www.foo.com", address);
    if (no_proxy_protocol) {
      info.reset();
    }

    options_ = std::make_shared<const Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, nullptr, std::move(info));

    setAddress();
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    EXPECT_CALL(*inner_socket_, ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(Const(*inner_socket_), ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));

    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    connect_socket_ =
        std::make_unique<UpstreamHttp11ConnectSocket>(std::move(inner_socket), options_);
    connect_socket_->setTransportSocketCallbacks(transport_callbacks_);
    connect_socket_->onConnected();
  }

  void setAddress() {
    std::string address_string =
        absl::StrCat(Network::Test::getLoopbackAddressUrlString(GetParam()), ":1234");
    auto address = Network::Utility::parseInternetAddressAndPort(address_string);
    transport_callbacks_.connection_.stream_info_.filterState()->setData(
        "envoy.network.transport_socket.http_11_proxy.address",
        std::make_unique<Network::Http11ProxyInfoFilterState>("www.foo.com", address),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  Network::TransportSocketOptionsConstSharedPtr options_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<UpstreamHttp11ConnectSocket> connect_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  Buffer::OwnedImpl connect_data_{"CONNECT www.foo.com:443 HTTP/1.1\r\n\r\n"};
  Ssl::ConnectionInfoConstSharedPtr ssl_{
      std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>()};
};

// Test injects CONNECT only once
TEST_P(Http11ConnectTest, InjectsHeaderOnlyOnce) {
  initialize();

  EXPECT_CALL(io_handle_, write(BufferStringEqual(connect_data_.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) {
        auto length = buffer.length();
        buffer.drain(length);
        return Api::IoCallUint64Result(length, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
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
          return Api::IoCallUint64Result(
              0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                 Network::IoSocketError::deleteIoError));
        }));
    EXPECT_CALL(io_handle_, write(BufferStringEqual(connect_data_.toString())))
        .WillOnce(Invoke([&](Buffer::Instance& buffer) {
          auto length = buffer.length();
          buffer.drain(length);
          return Api::IoCallUint64Result(length, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
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
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EADDRNOTAVAIL),
                                                        Network::IoSocketError::deleteIoError));
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
        return Api::IoCallUint64Result(initial_data.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, absl::optional<uint64_t>) {
        buffer.add(connect);
        return Api::IoCallUint64Result(connect.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
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
        return Api::IoCallUint64Result(initial_data.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
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
      .WillOnce(Return(ByMove(
          Api::IoCallUint64Result({}, Api::IoErrorPtr(new Network::IoSocketError(EADDRNOTAVAIL),
                                                      Network::IoSocketError::deleteIoError)))));
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
        return Api::IoCallUint64Result(initial_data.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          connect.length(), Api::IoErrorPtr(new Network::IoSocketError(EADDRNOTAVAIL),
                                            Network::IoSocketError::deleteIoError)))));
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
        return Api::IoCallUint64Result(initial_data.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          connect.length() - 1, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
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
    return Api::IoCallUint64Result(2000, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
  }));
  EXPECT_CALL(io_handle_, read(_, _)).Times(0);
  EXPECT_CALL(*inner_socket_, doRead(_)).Times(0);

  Buffer::OwnedImpl buffer("");
  auto result = connect_socket_->doRead(buffer);
  EXPECT_EQ(Network::PostIoAction::Close, result.action_);
}

// If response is not 200 OK, read fails.
TEST_P(Http11ConnectTest, InvalidResponse) {
  initialize();

  std::string connect("HTTP/1.1 404 Not Found\r\n\r\n");
  std::string initial_data(connect + "follow up data");
  EXPECT_CALL(io_handle_, recv(_, 2000, MSG_PEEK))
      .WillOnce(Invoke([&initial_data](void* buffer, size_t, int) {
        memcpy(buffer, initial_data.data(), initial_data.length());
        return Api::IoCallUint64Result(initial_data.length(),
                                       Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
      }));
  absl::optional<uint64_t> expected_bytes(connect.length());
  EXPECT_CALL(io_handle_, read(_, expected_bytes))
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(
          connect.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));

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

// Test createTransportSocket returns nullptr if inner call returns nullptr
TEST_F(SocketFactoryTest, CreateSocketReturnsNullWhenInnerFactoryReturnsNull) {
  initialize();
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _)).WillOnce(testing::ReturnNull());
  ASSERT_EQ(nullptr, factory_->createTransportSocket(nullptr, nullptr));
}

TEST(ParseTest, TestValidResponse) {
  {
    Buffer::OwnedImpl buffer("HTTP/1.0 200 OK\r\n\r\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(buffer));
    EXPECT_EQ(buffer.length(), 0);
  }
  {
    Buffer::OwnedImpl buffer("HTTP/1.0 200 OK\r\nFoo: Bar\r\n\r\n");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(buffer));
    EXPECT_EQ(buffer.length(), 0);
  }
  {
    Buffer::OwnedImpl buffer("HTTP/1.1   200  OK \r\n\r\nasdf");
    ASSERT_TRUE(UpstreamHttp11ConnectSocket::isValidConnectResponse(buffer));
    EXPECT_EQ(buffer.length(), 4);
    EXPECT_EQ(buffer.toString(), "asdf");
  }
  {
    Buffer::OwnedImpl buffer("HTTP/1.0 300 OK\r\n\r\n");
    ASSERT_FALSE(UpstreamHttp11ConnectSocket::isValidConnectResponse(buffer));
  }
}

// The SelfContainedParser is only intended for header parsing but for coverage,
// test a request with a body.
TEST(ParseTest, CoverResponseBody) {
  std::string headers = "HTTP/1.0 200 OK\r\ncontent-length: 2\r\n\r\n";
  std::string body = "ab";

  SelfContainedParser parser;
  parser.parser().execute(headers.c_str(), headers.length());
  parser.parser().execute(body.c_str(), body.length());
}

} // namespace
} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
