#include <functional>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"
#include "source/server/connection_handler_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::ElementsAre;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {
namespace {

// Build again on the basis of the connection_handler_test.cc

class ProxyProtocolTest : public testing::TestWithParam<Network::Address::IpVersion>,
                          public Network::ListenerConfig,
                          public Network::FilterChainManager,
                          protected Logger::Loggable<Logger::Id::main> {
public:
  ProxyProtocolTest()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        socket_(std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
            Network::Test::getCanonicalLoopbackAddress(GetParam()))),
        connection_handler_(new Server::ConnectionHandlerImpl(*dispatcher_, absl::nullopt)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()),
        init_manager_(nullptr) {
    EXPECT_CALL(socket_factory_, socketType()).WillOnce(Return(Network::Socket::Type::Stream));
    EXPECT_CALL(socket_factory_, localAddress())
        .WillOnce(ReturnRef(socket_->addressProvider().localAddress()));
    EXPECT_CALL(socket_factory_, getListenSocket(_)).WillOnce(Return(socket_));
    connection_handler_->addListener(absl::nullopt, *this);
    conn_ = dispatcher_->createClientConnection(socket_->addressProvider().localAddress(),
                                                Network::Address::InstanceConstSharedPtr(),
                                                Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);
  }

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::ListenSocketFactory& listenSocketFactory() override { return socket_factory_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() const override { return 0; }
  std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
  bool continueOnListenerFiltersTimeout() const override { return false; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  ResourceLimit& openConnections() override { return open_connections_; }
  const std::string& name() const override { return name_; }
  Network::UdpListenerConfigOptRef udpListenerConfig() override {
    return Network::UdpListenerConfigOptRef();
  }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::UNSPECIFIED;
  }
  Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return empty_access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
  Init::Manager& initManager() override { return *init_manager_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect(bool read = true,
               const envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol*
                   proto_config = nullptr) {
    int expected_callbacks = 2;
    auto maybeExitDispatcher = [&]() -> void {
      expected_callbacks--;
      if (expected_callbacks == 0) {
        dispatcher_->exit();
      }
    };

    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              nullptr, std::make_unique<Filter>(std::make_shared<Config>(
                           listenerScope(), (nullptr != proto_config)
                                                ? *proto_config
                                                : envoy::extensions::filters::listener::
                                                      proxy_protocol::v3::ProxyProtocol())));
          maybeExitDispatcher();
          return true;
        }));
    conn_->connect();
    if (read) {
      read_filter_ = std::make_shared<NiceMock<Network::MockReadFilter>>();
      EXPECT_CALL(factory_, createNetworkFilterChain(_, _))
          .WillOnce(Invoke([&](Network::Connection& connection,
                               const std::vector<Network::FilterFactoryCb>&) -> bool {
            server_connection_ = &connection;
            connection.addConnectionCallbacks(server_callbacks_);
            connection.addReadFilter(read_filter_);
            return true;
          }));
    }
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { maybeExitDispatcher(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void write(const uint8_t* s, ssize_t l) {
    Buffer::OwnedImpl buf(s, l);
    conn_->write(buf, false);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf, false);
  }

  void expectData(std::string expected) {
    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> Network::FilterStatus {
          EXPECT_EQ(buffer.toString(), expected);
          buffer.drain(expected.length());
          dispatcher_->exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    conn_->close(Network::ConnectionCloseType::NoFlush);

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void expectProxyProtoError() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(stats_store_.counter("downstream_cx_proxy_proto_error").value(), 1);
  }

  Stats::TestUtil::TestStore stats_store_;
  Api::ApiPtr api_;
  BasicResourceLimitImpl open_connections_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_;
  Network::MockListenSocketFactory socket_factory_;
  Network::NopConnectionBalancerImpl connection_balancer_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  std::string name_;
  Api::OsSysCallsImpl os_sys_calls_actual_;
  const Network::FilterChainSharedPtr filter_chain_;
  const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
  std::unique_ptr<Init::Manager> init_manager_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ProxyProtocolTest, V1Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 253.253.253.253 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1.2.3.4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V1Minimal) {
  connect();
  write("PROXY UNKNOWN\r\nmore data");

  expectData("more data");

  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "127.0.0.1");
  } else {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "::1");
  }
  EXPECT_FALSE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2Basic) {
  // A well-formed ipv4/tcp message, no extensions
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1.2.3.4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1:2:3::4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2BasicV6) {
  // A well-formed ipv6/tcp message, no extensions
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                0x0a, 0x21, 0x22, 0x00, 0x24, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',
                                'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1:2:3::4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2UnsupportedAF) {
  // A well-formed message with an unsupported address family
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x41, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, ErrorRecv_2) {
  // A well formed v4/tcp message, no extensions, but introduce an error on recv (e.g. socket close)
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  // TODO(davinci26): Mocking should not be used to provide real system calls.
  EXPECT_CALL(os_sys_calls, connect(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
        return os_sys_calls_actual_.connect(sockfd, addr, addrlen);
      }));
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.writev(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.readv(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, _, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
            return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen)
                .return_value_;
          }));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
            return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
          }));
  EXPECT_CALL(os_sys_calls, shutdown(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int how) { return os_sys_calls_actual_.shutdown(sockfd, how); }));
  EXPECT_CALL(os_sys_calls, close(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](os_fd_t fd) {
    return os_sys_calls_actual_.close(fd);
  }));
  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
            return os_sys_calls_actual_.accept(sockfd, addr, addrlen);
          }));
  connect(false);
  write(buffer, sizeof(buffer));

  errno = 0;
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, ErrorRecv_1) {
  // A well formed v4/tcp message, no extensions, but introduce an error on recv()
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  // TODO(davinci26): Mocking should not be used to provide real system calls.
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, connect(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
        return os_sys_calls_actual_.connect(sockfd, addr, addrlen);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.writev(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.readv(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, _, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
            return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen)
                .return_value_;
          }));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
            return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
          }));
  EXPECT_CALL(os_sys_calls, shutdown(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int how) { return os_sys_calls_actual_.shutdown(sockfd, how); }));
  EXPECT_CALL(os_sys_calls, close(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](os_fd_t fd) {
    return os_sys_calls_actual_.close(fd);
  }));
  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
            return os_sys_calls_actual_.accept(sockfd, addr, addrlen);
          }));
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2NotLocalOrOnBehalf) {
  // An illegal command type: neither 'local' nor 'proxy' command
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x23, 0x1f, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2LocalConnection) {
  // A 'local' connection, e.g. health-checking, no address, no extensions
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55,
                                0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));
  expectData("more data");
  if (server_connection_->addressProvider().remoteAddress()->ip()->version() ==
      Envoy::Network::Address::IpVersion::v6) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "::1");
  } else if (server_connection_->addressProvider().remoteAddress()->ip()->version() ==
             Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "127.0.0.1");
  }
  EXPECT_FALSE(server_connection_->addressProvider().localAddressRestored());
  disconnect();
}

TEST_P(ProxyProtocolTest, V2LocalConnectionExtension) {
  // A 'local' connection, e.g. health-checking, no address, 1 TLV (0x00,0x00,0x01,0xff) is present.
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x20, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0xff,
                                'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));
  expectData("more data");
  if (server_connection_->addressProvider().remoteAddress()->ip()->version() ==
      Envoy::Network::Address::IpVersion::v6) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "::1");
  } else if (server_connection_->addressProvider().remoteAddress()->ip()->version() ==
             Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
              "127.0.0.1");
  }
  EXPECT_FALSE(server_connection_->addressProvider().localAddressRestored());
  disconnect();
}

TEST_P(ProxyProtocolTest, V2ShortV4) {
  // An ipv4/tcp connection that has incorrect addr-len encoded
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x21, 0x00, 0x04, 0x00, 0x08, 0x00, 0x02,
                                'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2ShortAddrV4) {
  // An ipv4/tcp connection that has insufficient header-length encoded
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0b, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2ShortV6) {
  // An ipv6/tcp connection that has incorrect addr-len encoded
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x22, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2ShortAddrV6) {
  // An ipv6/tcp connection that has insufficient header-length encoded
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                0x0a, 0x21, 0x22, 0x00, 0x23, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',
                                'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2AF_UNIX) {
  // A well-formed AF_UNIX (0x32 in b14) connection is rejected
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x32, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2BadCommand) {
  // A non local/proxy command (0x29 in b13) is rejected
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x29, 0x32, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2WrongVersion) {
  // A non '2' version is rejected (0x93 in b13)
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x93, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V1TooLong) {
  constexpr uint8_t buffer[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  connect(false);
  write("PROXY TCP4 1.2.3.4 2.3.4.5 100 100");
  for (size_t i = 0; i < 256; i += sizeof(buffer)) {
    write(buffer, sizeof(buffer));
  }
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2ParseExtensions) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x14, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x1, 0xff};

  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  connect();
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  for (int i = 0; i < 2; i++) {
    write(tlv, sizeof(tlv));
  }
  write(data, sizeof(data));
  expectData("DATA");
  disconnect();
}

TEST_P(ProxyProtocolTest, V2ParseExtensionsRecvError) {
  // A well-formed ipv4/tcp with a TLV extension. An error is returned on tlv recv()
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x1, 0xff};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  // TODO(davinci26): Mocking should not be used to provide real system calls.
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, void* buf, size_t n, int flags) {
        const Api::SysCallSizeResult x = os_sys_calls_actual_.recv(fd, buf, n, flags);
        if (x.return_value_ == sizeof(tlv)) {
          return Api::SysCallSizeResult{-1, 0};
        } else {
          return x;
        }
      }));
  EXPECT_CALL(os_sys_calls, connect(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
        return os_sys_calls_actual_.connect(sockfd, addr, addrlen);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.writev(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.readv(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, _, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
            return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen)
                .return_value_;
          }));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
            return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
          }));
  EXPECT_CALL(os_sys_calls, shutdown(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int how) { return os_sys_calls_actual_.shutdown(sockfd, how); }));
  EXPECT_CALL(os_sys_calls, close(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](os_fd_t fd) {
    return os_sys_calls_actual_.close(fd);
  }));
  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
            return os_sys_calls_actual_.accept(sockfd, addr, addrlen);
          }));
  connect(false);
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(tlv, sizeof(tlv));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2ParseExtensionsFrag) {
  // A well-formed ipv4/tcp header with 2 TLV/extensions, these are fragmented on delivery
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x14, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x1, 0xff};

  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  connect();
  write(buffer, sizeof(buffer));
  for (int i = 0; i < 2; i++) {
    write(tlv, sizeof(tlv));
  }
  write(data, sizeof(data));
  expectData("DATA");
  disconnect();
}

TEST_P(ProxyProtocolTest, Fragmented) {
  connect();
  write("PROXY TCP4");
  write(" 254.254.2");
  write("54.254 1.2");
  write(".3.4 65535");
  write(" 1234\r\n...");

  // If there is no data after the PROXY line, the read filter does not receive even the
  // onNewConnection() callback. We need this in order to run the dispatcher in blocking
  // mode to make sure that proxy protocol processing is completed before we start testing
  // the results. Since we must have data we might as well check that we get it.
  expectData("...");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "254.254.254.254");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2Fragmented1) {
  // A well-formed ipv4/tcp header, delivering part of the signature, then part of
  // the address, then the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, 10);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 10, 10);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 20, 17);

  expectData("more data");
  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1.2.3.4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2Fragmented2) {
  // A well-formed ipv4/tcp header, delivering all of the signature + 1, then the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, 17);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 17, 10);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 27, 10);

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1.2.3.4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2Fragmented3Error) {
  // A well-formed ipv4/tcp header, delivering all of the signature +1, w/ an error
  // simulated in recv() on the +1
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  // TODO(davinci26): Mocking should not be used to provide real system calls.
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, void* buf, size_t len, int flags) {
        return os_sys_calls_actual_.recv(fd, buf, len, flags);
      }));
  EXPECT_CALL(os_sys_calls, recv(_, _, 1, _))
      .Times(AnyNumber())
      .WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, connect(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
        return os_sys_calls_actual_.connect(sockfd, addr, addrlen);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.writev(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.readv(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, _, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
            return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen)
                .return_value_;
          }));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
            return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
          }));
  EXPECT_CALL(os_sys_calls, shutdown(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int how) { return os_sys_calls_actual_.shutdown(sockfd, how); }));
  EXPECT_CALL(os_sys_calls, close(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](os_fd_t fd) {
    return os_sys_calls_actual_.close(fd);
  }));
  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
            return os_sys_calls_actual_.accept(sockfd, addr, addrlen);
          }));
  connect(false);
  write(buffer, 17);

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2Fragmented4Error) {
  // A well-formed ipv4/tcp header, part of the signature with an error introduced
  // in recv() on the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  // TODO(davinci26): Mocking should not be used to provide real system calls.
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, void* buf, size_t len, int flags) {
        return os_sys_calls_actual_.recv(fd, buf, len, flags);
      }));
  EXPECT_CALL(os_sys_calls, recv(_, _, 4, _))
      .Times(AnyNumber())
      .WillOnce(Return(Api::SysCallSizeResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, connect(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
        return os_sys_calls_actual_.connect(sockfd, addr, addrlen);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.writev(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([this](os_fd_t fd, const iovec* iov, int iovcnt) {
        return os_sys_calls_actual_.readv(fd, iov, iovcnt);
      }));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, _, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
            return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen)
                .return_value_;
          }));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
            return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
          }));
  EXPECT_CALL(os_sys_calls, shutdown(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, int how) { return os_sys_calls_actual_.shutdown(sockfd, how); }));
  EXPECT_CALL(os_sys_calls, close(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](os_fd_t fd) {
    return os_sys_calls_actual_.close(fd);
  }));
  EXPECT_CALL(os_sys_calls, accept(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [this](os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) -> Api::SysCallSocketResult {
            return os_sys_calls_actual_.accept(sockfd, addr, addrlen);
          }));
  connect(false);
  write(buffer, 10);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 10, 10);

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, PartialRead) {
  connect();

  write("PROXY TCP4");
  write(" 254.254.2");

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  write("54.254 1.2");
  write(".3.4 65535");
  write(" 1234\r\n...");

  expectData("...");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "254.254.254.254");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, V2PartialRead) {
  // A well-formed ipv4/tcp header, delivered with part of the signature,
  // part of the header, rest of header + body
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55,
                                0x49, 0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02,
                                0x03, 0x04, 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00,
                                0x02, 'm',  'o',  'r',  'e',  'd',  'a',  't',  'a'};
  connect();

  for (size_t i = 0; i < sizeof(buffer); i += 9) {
    write(&buffer[i], 9);
    if (i == 0) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  expectData("moredata");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->ip()->addressAsString(),
            "1.2.3.4");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

const std::string ProxyProtocol = "envoy.filters.listener.proxy_protocol";

TEST_P(ProxyProtocolTest, V2ExtractTlvOfInterest) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x1a, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv1[] = {0x0, 0x0, 0x1, 0xff};
  constexpr uint8_t tlv_type_authority[] = {0x02, 0x00, 0x07, 0x66, 0x6f,
                                            0x6f, 0x2e, 0x63, 0x6f, 0x6d};
  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule = proto_config.add_rules();
  rule->set_tlv_type(0x02);
  rule->mutable_on_tlv_present()->set_key("PP2 type authority");

  connect(true, &proto_config);
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  write(tlv1, sizeof(tlv1));
  write(tlv_type_authority, sizeof(tlv_type_authority));
  write(data, sizeof(data));
  expectData("DATA");

  EXPECT_EQ(1, server_connection_->streamInfo().dynamicMetadata().filter_metadata_size());

  auto metadata = server_connection_->streamInfo().dynamicMetadata().filter_metadata();
  EXPECT_EQ(1, metadata.size());
  EXPECT_EQ(1, metadata.count(ProxyProtocol));

  auto fields = metadata.at(ProxyProtocol).fields();
  EXPECT_EQ(1, fields.size());
  EXPECT_EQ(1, fields.count("PP2 type authority"));

  auto value_s = fields.at("PP2 type authority").string_value();
  ASSERT_THAT(value_s, ElementsAre(0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d));
  disconnect();
}

TEST_P(ProxyProtocolTest, V2ExtractTlvOfInterestAndEmitWithSpecifiedMetadataNamespace) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x1a, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv1[] = {0x0, 0x0, 0x1, 0xff};
  constexpr uint8_t tlv_type_authority[] = {0x02, 0x00, 0x07, 0x66, 0x6f,
                                            0x6f, 0x2e, 0x63, 0x6f, 0x6d};
  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule = proto_config.add_rules();
  rule->set_tlv_type(0x02);
  rule->mutable_on_tlv_present()->set_key("PP2 type authority");
  rule->mutable_on_tlv_present()->set_metadata_namespace("We need a different metadata namespace");

  connect(true, &proto_config);
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  write(tlv1, sizeof(tlv1));
  write(tlv_type_authority, sizeof(tlv_type_authority));
  write(data, sizeof(data));
  expectData("DATA");

  EXPECT_EQ(1, server_connection_->streamInfo().dynamicMetadata().filter_metadata_size());

  auto metadata = server_connection_->streamInfo().dynamicMetadata().filter_metadata();
  EXPECT_EQ(1, metadata.size());
  EXPECT_EQ(1, metadata.count("We need a different metadata namespace"));

  auto fields = metadata.at("We need a different metadata namespace").fields();
  EXPECT_EQ(1, fields.size());
  EXPECT_EQ(1, fields.count("PP2 type authority"));

  auto value_s = fields.at("PP2 type authority").string_value();
  ASSERT_THAT(value_s, ElementsAre(0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d));
  disconnect();
}

TEST_P(ProxyProtocolTest, V2ExtractMultipleTlvsOfInterest) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x39, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  // a TLV of type 0x00 with size of 4 (1 byte is value)
  constexpr uint8_t tlv1[] = {0x00, 0x00, 0x01, 0xff};
  // a TLV of type 0x02 with size of 10 bytes (7 bytes are value)
  constexpr uint8_t tlv_type_authority[] = {0x02, 0x00, 0x07, 0x66, 0x6f,
                                            0x6f, 0x2e, 0x63, 0x6f, 0x6d};
  // a TLV of type 0x0f with size of 6 bytes (3 bytes are value)
  constexpr uint8_t tlv3[] = {0x0f, 0x00, 0x03, 0xf0, 0x00, 0x0f};
  // a TLV of type 0xea with size of 25 bytes (22 bytes are value)
  constexpr uint8_t tlv_vpc_id[] = {0xea, 0x00, 0x16, 0x01, 0x76, 0x70, 0x63, 0x2d, 0x30,
                                    0x32, 0x35, 0x74, 0x65, 0x73, 0x74, 0x32, 0x66, 0x61,
                                    0x36, 0x63, 0x36, 0x33, 0x68, 0x61, 0x37};
  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule_type_authority = proto_config.add_rules();
  rule_type_authority->set_tlv_type(0x02);
  rule_type_authority->mutable_on_tlv_present()->set_key("PP2 type authority");

  auto rule_vpc_id = proto_config.add_rules();
  rule_vpc_id->set_tlv_type(0xea);
  rule_vpc_id->mutable_on_tlv_present()->set_key("PP2 vpc id");

  connect(true, &proto_config);
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  write(tlv1, sizeof(tlv1));
  write(tlv_type_authority, sizeof(tlv_type_authority));
  write(tlv3, sizeof(tlv3));
  write(tlv_vpc_id, sizeof(tlv_vpc_id));
  write(data, sizeof(data));
  expectData("DATA");

  EXPECT_EQ(1, server_connection_->streamInfo().dynamicMetadata().filter_metadata_size());

  auto metadata = server_connection_->streamInfo().dynamicMetadata().filter_metadata();
  EXPECT_EQ(1, metadata.size());
  EXPECT_EQ(1, metadata.count(ProxyProtocol));

  auto fields = metadata.at(ProxyProtocol).fields();
  EXPECT_EQ(2, fields.size());
  EXPECT_EQ(1, fields.count("PP2 type authority"));
  EXPECT_EQ(1, fields.count("PP2 vpc id"));

  auto value_type_authority = fields.at("PP2 type authority").string_value();
  ASSERT_THAT(value_type_authority, ElementsAre(0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d));

  auto value_vpc_id = fields.at("PP2 vpc id").string_value();
  ASSERT_THAT(value_vpc_id,
              ElementsAre(0x01, 0x76, 0x70, 0x63, 0x2d, 0x30, 0x32, 0x35, 0x74, 0x65, 0x73, 0x74,
                          0x32, 0x66, 0x61, 0x36, 0x63, 0x36, 0x33, 0x68, 0x61, 0x37));
  disconnect();
}

TEST_P(ProxyProtocolTest, V2WillNotOverwriteTLV) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x2a, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  // a TLV of type 0x00 with size of 4 (1 byte is value)
  constexpr uint8_t tlv1[] = {0x00, 0x00, 0x01, 0xff};
  // a TLV of type 0x02 with size of 10 bytes (7 bytes are value)
  constexpr uint8_t tlv_type_authority1[] = {0x02, 0x00, 0x07, 0x66, 0x6f,
                                             0x6f, 0x2e, 0x63, 0x6f, 0x6d};
  // a TLV of type 0x0f with size of 6 bytes (3 bytes are value)
  constexpr uint8_t tlv3[] = {0x0f, 0x00, 0x03, 0xf0, 0x00, 0x0f};
  // a TLV of type 0x02 (again) with size of 10 bytes (7 bytes are value) and different values
  constexpr uint8_t tlv_type_authority2[] = {0x02, 0x00, 0x07, 0x62, 0x61,
                                             0x72, 0x2e, 0x6e, 0x65, 0x74};
  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule_type_authority = proto_config.add_rules();
  rule_type_authority->set_tlv_type(0x02);
  rule_type_authority->mutable_on_tlv_present()->set_key("PP2 type authority");

  connect(true, &proto_config);
  write(buffer, sizeof(buffer));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  write(tlv1, sizeof(tlv1));
  write(tlv_type_authority1, sizeof(tlv_type_authority1));
  write(tlv3, sizeof(tlv3));
  write(tlv_type_authority2, sizeof(tlv_type_authority2));
  write(data, sizeof(data));
  expectData("DATA");

  EXPECT_EQ(1, server_connection_->streamInfo().dynamicMetadata().filter_metadata_size());

  auto metadata = server_connection_->streamInfo().dynamicMetadata().filter_metadata();
  EXPECT_EQ(1, metadata.size());
  EXPECT_EQ(1, metadata.count(ProxyProtocol));

  auto fields = metadata.at(ProxyProtocol).fields();
  EXPECT_EQ(1, fields.size());
  EXPECT_EQ(1, fields.count("PP2 type authority"));

  auto value_type_authority = fields.at("PP2 type authority").string_value();
  ASSERT_THAT(value_type_authority, ElementsAre(0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d));

  disconnect();
}

TEST_P(ProxyProtocolTest, V2WrongTLVLength) {
  // A well-formed ipv4/tcp with buffer[14]15] being 0x00 and 0x10. It says we should have 16 bytes
  // following.
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};

  // tlv[2] should be 0x1 since there's only one byte for tlv value.
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x2, 0xff};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule_00 = proto_config.add_rules();
  rule_00->set_tlv_type(0x00);
  rule_00->mutable_on_tlv_present()->set_key("00");

  connect(false, &proto_config);
  write(buffer, sizeof(buffer));
  write(tlv, sizeof(tlv));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, V2IncompleteTLV) {
  // A ipv4/tcp with buffer[14]15] being 0x00 and 0x11. It says we should have 17 bytes following,
  // however we have 20.
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x11, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};

  // a TLV of type 0x00 with size of 4 (1 byte is value)
  constexpr uint8_t tlv1[] = {0x0, 0x0, 0x1, 0xff};
  // a TLV of type 0x01 with size of 4 (1 byte is value)
  constexpr uint8_t tlv2[] = {0x1, 0x0, 0x1, 0xff};

  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proto_config;
  auto rule_00 = proto_config.add_rules();
  rule_00->set_tlv_type(0x00);
  rule_00->mutable_on_tlv_present()->set_key("00");

  auto rule_01 = proto_config.add_rules();
  rule_01->set_tlv_type(0x01);
  rule_01->mutable_on_tlv_present()->set_key("01");

  connect(false, &proto_config);
  write(buffer, sizeof(buffer));
  write(tlv1, sizeof(tlv1));
  write(tlv2, sizeof(tlv2));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, MalformedProxyLine) {
  connect(false);

  write("BOGUS\r");
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  write("\n");

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, ProxyLineTooLarge) {
  connect(false);
  write("012345678901234567890123456789012345678901234567890123456789"
        "012345678901234567890123456789012345678901234567890123456789");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, NotEnoughFields) {
  connect(false);
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, UnsupportedProto) {
  connect(false);
  write("PROXY UDP6 1:2:3::4 5:6::7:8 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, InvalidSrcAddress) {
  connect(false);
  write("PROXY TCP4 230.0.0.1 10.1.1.3 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, InvalidDstAddress) {
  connect(false);
  write("PROXY TCP4 10.1.1.2 0.0.0.0 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, BadPort) {
  connect(false);
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234 abc\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, NegativePort) {
  connect(false);
  write("PROXY TCP6 1:2:3::4 5:6::7:8 -1 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, PortOutOfRange) {
  connect(false);
  write("PROXY TCP6 1:2:3::4 5:6::7:8 66776 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, BadAddress) {
  connect(false);
  write("PROXY TCP6 1::2:3::4 5:6::7:8 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, AddressVersionsNotMatch) {
  connect(false);
  write("PROXY TCP4 [1:2:3::4] 1.2.3.4 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, AddressVersionsNotMatch2) {
  connect(false);
  write("PROXY TCP4 1.2.3.4 [1:2:3: 1234 4]:5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, Truncated) {
  connect(false);
  write("PROXY TCP4 1.2.3.4 5.6.7.8 1234 5678");
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  conn_->close(Network::ConnectionCloseType::NoFlush);

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, Closed) {
  connect(false);
  write("PROXY TCP4 1.2.3");
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  conn_->close(Network::ConnectionCloseType::NoFlush);

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, ClosedEmpty) {
  // We may or may not get these, depending on the operating system timing.
  EXPECT_CALL(factory_, createListenerFilterChain(_)).Times(AtLeast(0));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).Times(AtLeast(0));
  conn_->connect();
  conn_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

class WildcardProxyProtocolTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public Network::ListenerConfig,
                                  public Network::FilterChainManager,
                                  protected Logger::Loggable<Logger::Id::main> {
public:
  WildcardProxyProtocolTest()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        socket_(std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
            Network::Test::getAnyAddress(GetParam()))),
        local_dst_address_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(GetParam()),
            socket_->addressProvider().localAddress()->ip()->port())),
        connection_handler_(new Server::ConnectionHandlerImpl(*dispatcher_, absl::nullopt)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()),
        init_manager_(nullptr) {
    EXPECT_CALL(socket_factory_, socketType()).WillOnce(Return(Network::Socket::Type::Stream));
    EXPECT_CALL(socket_factory_, localAddress())
        .WillOnce(ReturnRef(socket_->addressProvider().localAddress()));
    EXPECT_CALL(socket_factory_, getListenSocket(_)).WillOnce(Return(socket_));
    connection_handler_->addListener(absl::nullopt, *this);
    conn_ = dispatcher_->createClientConnection(local_dst_address_,
                                                Network::Address::InstanceConstSharedPtr(),
                                                Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);

    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              nullptr,
              std::make_unique<Filter>(std::make_shared<Config>(
                  listenerScope(),
                  envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol())));
          return true;
        }));
  }

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::ListenSocketFactory& listenSocketFactory() override { return socket_factory_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() const override { return 0; }
  std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
  ResourceLimit& openConnections() override { return open_connections_; }
  bool continueOnListenerFiltersTimeout() const override { return false; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }
  Network::UdpListenerConfigOptRef udpListenerConfig() override {
    return Network::UdpListenerConfigOptRef();
  }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::UNSPECIFIED;
  }
  Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return empty_access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
  Init::Manager& initManager() override { return *init_manager_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect() {
    conn_->connect();
    read_filter_ = std::make_shared<NiceMock<Network::MockReadFilter>>();
    EXPECT_CALL(factory_, createNetworkFilterChain(_, _))
        .WillOnce(Invoke([&](Network::Connection& connection,
                             const std::vector<Network::FilterFactoryCb>&) -> bool {
          server_connection_ = &connection;
          connection.addConnectionCallbacks(server_callbacks_);
          connection.addReadFilter(read_filter_);
          return true;
        }));
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf, false);
  }

  void expectData(std::string expected) {
    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> Network::FilterStatus {
          EXPECT_EQ(buffer.toString(), expected);
          buffer.drain(expected.length());
          dispatcher_->exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    conn_->close(Network::ConnectionCloseType::NoFlush);
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  BasicResourceLimitImpl open_connections_;
  Network::MockListenSocketFactory socket_factory_;
  std::shared_ptr<Network::TcpListenSocket> socket_;
  Network::Address::InstanceConstSharedPtr local_dst_address_;
  Network::NopConnectionBalancerImpl connection_balancer_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  std::string name_;
  const Network::FilterChainSharedPtr filter_chain_;
  const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
  std::unique_ptr<Init::Manager> init_manager_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_SUITE_P(IpVersions, WildcardProxyProtocolTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(WildcardProxyProtocolTest, Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->asString(), "1.2.3.4:65535");
  EXPECT_EQ(server_connection_->addressProvider().localAddress()->asString(),
            "254.254.254.254:1234");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST_P(WildcardProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->addressProvider().remoteAddress()->asString(), "[1:2:3::4]:65535");
  EXPECT_EQ(server_connection_->addressProvider().localAddress()->asString(), "[5:6::7:8]:1234");
  EXPECT_TRUE(server_connection_->addressProvider().localAddressRestored());

  disconnect();
}

TEST(ProxyProtocolConfigFactoryTest, TestCreateFactory) {
  Server::Configuration::NamedListenerFilterConfigFactory* factory = Registry::FactoryRegistry<
      Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(ProxyProtocol);

  EXPECT_EQ(factory->name(), ProxyProtocol);

  const std::string yaml = R"EOF(
      rules:
        - tlv_type: 0x01
          on_tlv_present:
            key: "PP2_TYPE_ALPN"
        - tlv_type: 0x1a
          on_tlv_present:
            key: "PP2_TYPE_CUSTOMER_A"
)EOF";

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  Server::Configuration::MockListenerFactoryContext context;
  EXPECT_CALL(context, scope());
  EXPECT_CALL(context, messageValidationVisitor());
  Network::ListenerFilterFactoryCb cb =
      factory->createListenerFilterFactoryFromProto(*proto_config, nullptr, context);

  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ProxyProtocol::Filter*>(added_filter.get()), nullptr);
}

// Test that the deprecated extension name still functions.
TEST(ProxyProtocolConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.proxy_protocol";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
