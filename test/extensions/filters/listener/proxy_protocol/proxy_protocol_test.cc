#include <functional>
#include <memory>
#include <string>

#include "envoy/stats/stats.h"

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#include "server/connection_handler_impl.h"

#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

// Build again on the basis of the connection_handler_test.cc

class ProxyProtocolTest : public testing::TestWithParam<Network::Address::IpVersion>,
                          public Network::ListenerConfig,
                          public Network::FilterChainManager,
                          protected Logger::Loggable<Logger::Id::main> {
public:
  ProxyProtocolTest()
      : socket_(Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true),
        connection_handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()) {

    connection_handler_->addListener(*this);
    conn_ = dispatcher_.createClientConnection(socket_.localAddress(),
                                               Network::Address::InstanceConstSharedPtr(),
                                               Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);
  }

  // Listener
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::Socket& socket() override { return socket_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() override { return 0; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect(bool read = true) {
    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              std::make_unique<Filter>(std::make_shared<Config>(listenerScope())));
          return true;
        }));
    conn_->connect();
    if (read) {
      read_filter_.reset(new NiceMock<Network::MockReadFilter>());
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
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
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
          dispatcher_.exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    conn_->close(Network::ConnectionCloseType::NoFlush);

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void expectProxyProtoError() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(stats_store_.counter("downstream_cx_proxy_proto_error").value(), 1);
  }

  Event::DispatcherImpl dispatcher_;
  Network::TcpListenSocket socket_;
  Stats::IsolatedStoreImpl stats_store_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  std::string name_;
  const Network::FilterChainSharedPtr filter_chain_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, ProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(ProxyProtocolTest, v1Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 253.253.253.253 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v1Minimal) {
  connect();
  write("PROXY UNKNOWN\r\nmore data");

  expectData("more data");

  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "127.0.0.1");
  } else {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "::1");
  }
  EXPECT_FALSE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2Basic) {
  // A well-formed ipv4/tcp message, no extensions
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1:2:3::4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2BasicV6) {
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

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1:2:3::4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2UnsupportedAF) {
  // A well-formed message with an unsupported address family
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x41, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, errorRecv_2) {
  // A well formed v4/tcp message, no extensions, but introduce an error on recv (e.g. socket close)
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).Times(AnyNumber()).WillOnce(Return((errno = 0, -1)));
  EXPECT_CALL(os_sys_calls, ioctl(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([](int fd, unsigned long int request, void* argp) {
        return ::ioctl(fd, request, argp);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::writev(fd, iov, iovcnt); }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::readv(fd, iov, iovcnt); }));

  connect(false);
  write(buffer, sizeof(buffer));

  errno = 0;
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, errorFIONREAD_1) {
  // A well formed v4/tcp message, no extensions, but introduce an error on ioctl(...FIONREAD...)
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, ioctl(_, FIONREAD, _)).WillOnce(Return(-1));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::writev(fd, iov, iovcnt); }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::readv(fd, iov, iovcnt); }));

  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2NotLocalOrOnBehalf) {
  // An illegal command type: neither 'local' nor 'proxy' command
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x23, 0x1f, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2LocalConnection) {
  // A 'local' connection, e.g. health-checking, no address, no extensions
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55,
                                0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));
  expectData("more data");
  if (server_connection_->remoteAddress()->ip()->version() ==
      Envoy::Network::Address::IpVersion::v6) {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "::1");
  } else if (server_connection_->remoteAddress()->ip()->version() ==
             Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "127.0.0.1");
  }
  EXPECT_FALSE(server_connection_->localAddressRestored());
  disconnect();
}

TEST_P(ProxyProtocolTest, v2LocalConnectionExtension) {
  // A 'local' connection, e.g. health-checking, no address, 1 TLV (0x00,0x00,0x01,0xff) is present.
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x20, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0xff,
                                'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, sizeof(buffer));
  expectData("more data");
  if (server_connection_->remoteAddress()->ip()->version() ==
      Envoy::Network::Address::IpVersion::v6) {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "::1");
  } else if (server_connection_->remoteAddress()->ip()->version() ==
             Envoy::Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "127.0.0.1");
  }
  EXPECT_FALSE(server_connection_->localAddressRestored());
  disconnect();
}

TEST_P(ProxyProtocolTest, v2ShortV4) {
  // An ipv4/tcp connection that has incorrect addr-len encoded
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x21, 0x00, 0x04, 0x00, 0x08, 0x00, 0x02,
                                'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2ShortAddrV4) {
  // An ipv4/tcp connection that has insufficient header-length encoded
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0b, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2ShortV6) {
  // An ipv6/tcp connection that has incorrect addr-len encoded
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x22, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);

  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2ShortAddrV6) {
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

TEST_P(ProxyProtocolTest, v2AF_UNIX) {
  // A well-formed AF_UNIX (0x32 in b14) connection is rejected
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x32, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2BadCommand) {
  // A non local/proxy command (0x29 in b13) is rejected
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x29, 0x32, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2WrongVersion) {
  // A non '2' version is rejected (0x93 in b13)
  constexpr uint8_t buffer[] = {
      0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21, 0x93, 0x00,
      0x14, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 'm',  'o',  'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect(false);
  write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v1TooLong) {
  constexpr uint8_t buffer[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  connect(false);
  write("PROXY TCP4 1.2.3.4 2.3.4.5 100 100");
  for (size_t i = 0; i < 256; i += sizeof(buffer))
    write(buffer, sizeof(buffer));
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2ParseExtensions) {
  // A well-formed ipv4/tcp with a pair of TLV extensions is accepted
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x14, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x1, 0xff};

  constexpr uint8_t data[] = {'D', 'A', 'T', 'A'};

  connect();
  write(buffer, sizeof(buffer));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  for (int i = 0; i < 2; i++) {
    write(tlv, sizeof(tlv));
  }
  write(data, sizeof(data));
  expectData("DATA");
  disconnect();
}

TEST_P(ProxyProtocolTest, v2ParseExtensionsIoctlError) {
  // A well-formed ipv4/tcp with a TLV extension. An error is created in the ioctl(...FIONREAD...)
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x10, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02};
  constexpr uint8_t tlv[] = {0x0, 0x0, 0x1, 0xff};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, ioctl(_, FIONREAD, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([](int fd, unsigned long int request, void* argp) {
        int x = ::ioctl(fd, request, argp);
        if (x == 0 && *static_cast<int*>(argp) == sizeof(tlv)) {
          return -1;
        } else {
          return x;
        }
      }));

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, void* buf, size_t len, int flags) { return ::recv(fd, buf, len, flags); }));

  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::writev(fd, iov, iovcnt); }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::readv(fd, iov, iovcnt); }));

  connect(false);
  write(buffer, sizeof(buffer));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(tlv, sizeof(tlv));

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2ParseExtensionsFrag) {
  // A well-formed ipv4/tcp header with 2 TLV/extenions, these are fragmented on delivery
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

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "254.254.254.254");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2Fragmented1) {
  // A well-formed ipv4/tcp header, delivering part of the signature, then part of
  // the address, then the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, 10);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 10, 10);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 20, 17);

  expectData("more data");
  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2Fragmented2) {
  // A well-formed ipv4/tcp header, delivering all of the signature + 1, then the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};
  connect();
  write(buffer, 17);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 17, 10);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 27, 10);

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2Fragmented3Error) {
  // A well-formed ipv4/tcp header, delivering all of the signature +1, w/ an error
  // simulated in recv() on the +1
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, void* buf, size_t len, int flags) { return ::recv(fd, buf, len, flags); }));
  EXPECT_CALL(os_sys_calls, recv(_, _, 1, _)).Times(AnyNumber()).WillOnce(Return(-1));

  EXPECT_CALL(os_sys_calls, ioctl(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([](int fd, unsigned long int request, void* argp) {
        return ::ioctl(fd, request, argp);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::writev(fd, iov, iovcnt); }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::readv(fd, iov, iovcnt); }));

  connect(false);
  write(buffer, 17);

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, v2Fragmented4Error) {
  // A well-formed ipv4/tcp header, part of the signature with an error introduced
  // in recv() on the remainder
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 'm',  'o',
                                'r',  'e',  ' ',  'd',  'a',  't',  'a'};

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, void* buf, size_t len, int flags) { return ::recv(fd, buf, len, flags); }));
  EXPECT_CALL(os_sys_calls, recv(_, _, 4, _)).Times(AnyNumber()).WillOnce(Return(-1));

  EXPECT_CALL(os_sys_calls, ioctl(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke([](int fd, unsigned long int request, void* argp) {
        return ::ioctl(fd, request, argp);
      }));
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::writev(fd, iov, iovcnt); }));
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(
          [](int fd, const struct iovec* iov, int iovcnt) { return ::readv(fd, iov, iovcnt); }));

  connect(false);
  write(buffer, 10);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write(buffer + 10, 10);

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, PartialRead) {
  connect();

  write("PROXY TCP4");
  write(" 254.254.2");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  write("54.254 1.2");
  write(".3.4 65535");
  write(" 1234\r\n...");

  expectData("...");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "254.254.254.254");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, v2PartialRead) {
  // A well-formed ipv4/tcp header, delivered with part of the signature,
  // part of the header, rest of header + body
  constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55,
                                0x49, 0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02,
                                0x03, 0x04, 0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x00,
                                0x02, 'm',  'o',  'r',  'e',  'd',  'a',  't',  'a'};
  connect();

  for (size_t i = 0; i < sizeof(buffer); i += 9) {
    write(&buffer[i], 9);
    if (i == 0)
      dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }

  expectData("moredata");

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(ProxyProtocolTest, MalformedProxyLine) {
  connect(false);

  write("BOGUS\r");
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
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
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
  conn_->close(Network::ConnectionCloseType::NoFlush);

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, Closed) {
  connect(false);
  write("PROXY TCP4 1.2.3");
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
  conn_->close(Network::ConnectionCloseType::NoFlush);

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, ClosedEmpty) {
  // We may or may not get these, depending on the operating system timing.
  EXPECT_CALL(factory_, createListenerFilterChain(_)).Times(AtLeast(0));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).Times(AtLeast(0));
  conn_->connect();
  conn_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

class WildcardProxyProtocolTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public Network::ListenerConfig,
                                  public Network::FilterChainManager,
                                  protected Logger::Loggable<Logger::Id::main> {
public:
  WildcardProxyProtocolTest()
      : socket_(Network::Test::getAnyAddress(GetParam()), nullptr, true),
        local_dst_address_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(GetParam()),
            socket_.localAddress()->ip()->port())),
        connection_handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)),
        name_("proxy"), filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()) {
    connection_handler_->addListener(*this);
    conn_ = dispatcher_.createClientConnection(local_dst_address_,
                                               Network::Address::InstanceConstSharedPtr(),
                                               Network::Test::createRawBufferSocket(), nullptr);
    conn_->addConnectionCallbacks(connection_callbacks_);

    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](Network::ListenerFilterManager& filter_manager) -> bool {
          filter_manager.addAcceptFilter(
              std::make_unique<Filter>(std::make_shared<Config>(listenerScope())));
          return true;
        }));
  }

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *this; }
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::Socket& socket() override { return socket_; }
  bool bindToPort() override { return true; }
  bool handOffRestoredDestinationConnections() const override { return false; }
  uint32_t perConnectionBufferLimitBytes() override { return 0; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  void connect() {
    conn_->connect();
    read_filter_.reset(new NiceMock<Network::MockReadFilter>());
    EXPECT_CALL(factory_, createNetworkFilterChain(_, _))
        .WillOnce(Invoke([&](Network::Connection& connection,
                             const std::vector<Network::FilterFactoryCb>&) -> bool {
          server_connection_ = &connection;
          connection.addConnectionCallbacks(server_callbacks_);
          connection.addReadFilter(read_filter_);
          return true;
        }));
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
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
          dispatcher_.exit();
          return Network::FilterStatus::Continue;
        }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    conn_->close(Network::ConnectionCloseType::NoFlush);
    EXPECT_CALL(server_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  Event::DispatcherImpl dispatcher_;
  Network::TcpListenSocket socket_;
  Network::Address::InstanceConstSharedPtr local_dst_address_;
  Stats::IsolatedStoreImpl stats_store_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  Network::ClientConnectionPtr conn_;
  NiceMock<Network::MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  std::string name_;
  const Network::FilterChainSharedPtr filter_chain_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, WildcardProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(WildcardProxyProtocolTest, Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->asString(), "1.2.3.4:65535");
  EXPECT_EQ(server_connection_->localAddress()->asString(), "254.254.254.254:1234");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

TEST_P(WildcardProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  expectData("more data");

  EXPECT_EQ(server_connection_->remoteAddress()->asString(), "[1:2:3::4]:65535");
  EXPECT_EQ(server_connection_->localAddress()->asString(), "[5:6::7:8]:1234");
  EXPECT_TRUE(server_connection_->localAddressRestored());

  disconnect();
}

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
