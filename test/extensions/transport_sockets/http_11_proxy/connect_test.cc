#include "envoy/config/core/v3/address.pb.h"

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

  void initialize(bool no_proxy_protocol = false, absl::optional<uint32_t> target_port = {}) {
    initializeInternal(no_proxy_protocol, false, target_port);
  }

  // Initialize the test with the proxy address provided via endpoint metadata.
  void initializeWithMetadataProxyAddr() { initializeInternal(false, true, {}); }

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

  void injectHeaderOnceTest() {
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

  Network::TransportSocketOptionsConstSharedPtr options_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<UpstreamHttp11ConnectSocket> connect_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  Buffer::OwnedImpl connect_data_{"CONNECT www.foo.com:443 HTTP/1.1\r\n\r\n"};
  Ssl::ConnectionInfoConstSharedPtr ssl_{
      std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>()};

private:
  void initializeInternal(bool no_proxy_protocol, bool use_metadata_proxy_addr,
                          absl::optional<uint32_t> target_port) {
    std::string address_string =
        absl::StrCat(Network::Test::getLoopbackAddressUrlString(GetParam()), ":1234");
    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::parseInternetAddressAndPortNoThrow(address_string);

    const std::string port = target_port.has_value() ? absl::StrCat(":", *target_port) : "";
    const std::string proxy_info_hostname = absl::StrCat("www.foo.com", port);
    auto host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    std::unique_ptr<Network::TransportSocketOptions::Http11ProxyInfo> info;

    if (!no_proxy_protocol) {
      if (use_metadata_proxy_addr) {
        // In the case of endpoint metadata configuring the proxy address, we expect the hostname
        // used to be that of the host.
        connect_data_ = Buffer::OwnedImpl{
            fmt::format("CONNECT {} HTTP/1.1\r\n\r\n", host->address()->asStringView())};

        auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
        const std::string metadata_key =
            Config::MetadataFilters::get().ENVOY_HTTP11_PROXY_TRANSPORT_SOCKET_ADDR;
        envoy::config::core::v3::Address addr_proto;
        addr_proto.mutable_socket_address()->set_address(proxy_info_hostname);
        addr_proto.mutable_socket_address()->set_port_value(1234);
        ProtobufWkt::Any anypb;
        anypb.PackFrom(addr_proto);
        metadata->mutable_typed_filter_metadata()->emplace(std::make_pair(metadata_key, anypb));
        EXPECT_CALL(*host, metadata()).Times(AnyNumber()).WillRepeatedly(Return(metadata));
      } else {
        info = std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>(
            proxy_info_hostname, address);
        setAddress();
      }
    }

    options_ = std::make_shared<const Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, nullptr, std::move(info));

    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    EXPECT_CALL(*inner_socket_, ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));
    EXPECT_CALL(Const(*inner_socket_), ssl()).Times(AnyNumber()).WillRepeatedly(Return(ssl_));

    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));

    connect_socket_ =
        std::make_unique<UpstreamHttp11ConnectSocket>(std::move(inner_socket), options_, host);
    connect_socket_->setTransportSocketCallbacks(transport_callbacks_);
    connect_socket_->onConnected();
  }
};

// Test injects CONNECT only once. Configured via transport socket options.
TEST_P(Http11ConnectTest, InjectsHeaderOnlyOnceTransportSocketOpts) {
  initialize();
  injectHeaderOnceTest();
}

TEST_P(Http11ConnectTest, HostWithPort) {
  initialize(false, 443);
  injectHeaderOnceTest();
}

TEST_P(Http11ConnectTest, ProxySslPortRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.proxy_ssl_port", "false"}});

  initialize();
  injectHeaderOnceTest();
}

// Test injects CONNECT only once. Configured via endpoint metadata.
TEST_P(Http11ConnectTest, InjectsHeaderOnlyOnceEndpointMetadata) {
  initializeWithMetadataProxyAddr();
  injectHeaderOnceTest();
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

} // namespace
} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
