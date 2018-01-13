#include <functional>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {

// Build again on the basis of the connection_handler_test.cc

class ProxyProtocolTest : public testing::TestWithParam<Address::IpVersion>,
                          public Server::Listener,
                          protected Logger::Loggable<Logger::Id::main> {
public:
  ProxyProtocolTest()
      : socket_(Network::Test::getCanonicalLoopbackAddress(GetParam()), true),
        connection_handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)),
        name_("proxy") {
    connection_handler_->addListener(*this);
    conn_ = dispatcher_.createClientConnection(socket_.localAddress(),
                                               Network::Address::InstanceConstSharedPtr(),
                                               Network::Test::createRawBufferSocket());
    conn_->addConnectionCallbacks(connection_callbacks_);

    ON_CALL(factory_, createListenerFilterChain(_)).WillByDefault(Return(true));
  }

  // Listener
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::ListenSocket& socket() override { return socket_; }
  Ssl::ServerContext* defaultSslContext() override { return nullptr; }
  bool useProxyProto() override { return true; }
  bool bindToPort() override { return true; }
  bool useOriginalDst() override { return false; }
  uint32_t perConnectionBufferLimitBytes() override { return 0; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }

  void connect() {
    conn_->connect();
    read_filter_.reset(new NiceMock<MockReadFilter>());
    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](ListenerFilterManager&) -> bool { return true; }));
    EXPECT_CALL(factory_, createNetworkFilterChain(_))
        .WillOnce(Invoke([&](Connection& connection) -> bool {
          server_connection_ = &connection;
          connection.addConnectionCallbacks(server_callbacks_);
          connection.addReadFilter(read_filter_);
          return true;
        }));
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void connectNoRead() {
    conn_->connect();
    EXPECT_CALL(factory_, createListenerFilterChain(_));
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::LocalClose));
    EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    conn_->close(ConnectionCloseType::NoFlush);

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void expectProxyProtoError() {
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(stats_store_.counter("downstream_cx_proxy_proto_error").value(), 1);
  }

  Event::DispatcherImpl dispatcher_;
  TcpListenSocket socket_;
  Stats::IsolatedStoreImpl stats_store_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  ClientConnectionPtr conn_;
  NiceMock<MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
  std::string name_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, ProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ProxyProtocolTest, Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 253.253.253.253 65535 1234\r\nmore data");

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> FilterStatus {
        EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1.2.3.4");

        EXPECT_EQ(TestUtility::bufferToString(buffer), "more data");
        buffer.drain(9);
        return Network::FilterStatus::Continue;
      }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  disconnect();
}

TEST_P(ProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> FilterStatus {
        EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "1:2:3::4");

        EXPECT_EQ(TestUtility::bufferToString(buffer), "more data");
        buffer.drain(9);
        return Network::FilterStatus::Continue;
      }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  disconnect();
}

TEST_P(ProxyProtocolTest, Fragmented) {
  connect();
  write("PROXY TCP4");
  write(" 254.254.2");
  write("54.254 1.2");
  write(".3.4 65535");
  write(" 1234\r\n");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "254.254.254.254");

  disconnect();
}

TEST_P(ProxyProtocolTest, PartialRead) {
  connect();

  write("PROXY TCP4");
  write(" 254.254.2");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  write("54.254 1.2");
  write(".3.4 65535");
  write(" 1234\r\n");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_EQ(server_connection_->remoteAddress()->ip()->addressAsString(), "254.254.254.254");

  disconnect();
}

TEST_P(ProxyProtocolTest, MalformedProxyLine) {
  connectNoRead();

  write("BOGUS\r");
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  write("\n");

  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, ProxyLineTooLarge) {
  connectNoRead();
  write("012345678901234567890123456789012345678901234567890123456789"
        "012345678901234567890123456789012345678901234567890123456789");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, NotEnoughFields) {
  connectNoRead();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, UnsupportedProto) {
  connectNoRead();
  write("PROXY UDP6 1:2:3::4 5:6::7:8 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, InvalidSrcAddress) {
  connectNoRead();
  write("PROXY TCP4 230.0.0.1 10.1.1.3 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, InvalidDstAddress) {
  connectNoRead();
  write("PROXY TCP4 10.1.1.2 0.0.0.0 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, BadPort) {
  connectNoRead();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234 abc\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, NegativePort) {
  connectNoRead();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 -1 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, PortOutOfRange) {
  connectNoRead();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 66776 1234\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, BadAddress) {
  connectNoRead();
  write("PROXY TCP6 1::2:3::4 5:6::7:8 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, AddressVersionsNotMatch) {
  connectNoRead();
  write("PROXY TCP4 [1:2:3::4] 1.2.3.4 1234 5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, AddressVersionsNotMatch2) {
  connectNoRead();
  write("PROXY TCP4 1.2.3.4 [1:2:3: 1234 4]:5678\r\nmore data");
  expectProxyProtoError();
}

TEST_P(ProxyProtocolTest, Truncated) {
  connectNoRead();
  write("PROXY TCP4 1.2.3.4 5.6.7.8 1234 5678");
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
  conn_->close(ConnectionCloseType::NoFlush);

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, Closed) {
  connectNoRead();
  write("PROXY TCP4 1.2.3");
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
  conn_->close(ConnectionCloseType::NoFlush);

  dispatcher_.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ProxyProtocolTest, ClosedEmpty) {
  conn_->connect();
  EXPECT_CALL(factory_, createListenerFilterChain(_));
  conn_->close(ConnectionCloseType::NoFlush);
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

class WildcardProxyProtocolTest : public testing::TestWithParam<Address::IpVersion>,
                                  public Server::Listener,
                                  protected Logger::Loggable<Logger::Id::main> {
public:
  WildcardProxyProtocolTest()
      : socket_(Network::Test::getAnyAddress(GetParam()), true),
        local_dst_address_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(GetParam()),
            socket_.localAddress()->ip()->port())),
        connection_handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)),
        name_("proxy") {
    connection_handler_->addListener(*this);
    conn_ = dispatcher_.createClientConnection(local_dst_address_,
                                               Network::Address::InstanceConstSharedPtr(),
                                               Network::Test::createRawBufferSocket());
    conn_->addConnectionCallbacks(connection_callbacks_);
  }

  // Server::Listener
  Network::FilterChainFactory& filterChainFactory() override { return factory_; }
  Network::ListenSocket& socket() override { return socket_; }
  Ssl::ServerContext* defaultSslContext() override { return nullptr; }
  bool useProxyProto() override { return true; }
  bool bindToPort() override { return true; }
  bool useOriginalDst() override { return false; }
  uint32_t perConnectionBufferLimitBytes() override { return 0; }
  Stats::Scope& listenerScope() override { return stats_store_; }
  uint64_t listenerTag() const override { return 1; }
  const std::string& name() const override { return name_; }

  void connect() {
    conn_->connect();
    read_filter_.reset(new NiceMock<MockReadFilter>());
    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillOnce(Invoke([&](ListenerFilterManager&) -> bool { return true; }));
    EXPECT_CALL(factory_, createNetworkFilterChain(_))
        .WillOnce(Invoke([&](Connection& connection) -> bool {
          server_connection_ = &connection;
          connection.addConnectionCallbacks(server_callbacks_);
          connection.addReadFilter(read_filter_);
          return true;
        }));
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));
    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf);
  }

  void disconnect() {
    EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::LocalClose));
    conn_->close(ConnectionCloseType::NoFlush);
    EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_.exit(); }));

    dispatcher_.run(Event::Dispatcher::RunType::Block);
  }

  Event::DispatcherImpl dispatcher_;
  TcpListenSocket socket_;
  Network::Address::InstanceConstSharedPtr local_dst_address_;
  Stats::IsolatedStoreImpl stats_store_;
  Network::ConnectionHandlerPtr connection_handler_;
  Network::MockFilterChainFactory factory_;
  ClientConnectionPtr conn_;
  NiceMock<MockConnectionCallbacks> connection_callbacks_;
  Network::Connection* server_connection_;
  Network::MockConnectionCallbacks server_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
  std::string name_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, WildcardProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(WildcardProxyProtocolTest, Basic) {
  connect();
  write("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\nmore data");

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> FilterStatus {
        EXPECT_EQ(server_connection_->remoteAddress()->asString(), "1.2.3.4:65535");
        EXPECT_EQ(server_connection_->localAddress()->asString(), "254.254.254.254:1234");

        EXPECT_EQ(TestUtility::bufferToString(buffer), "more data");
        buffer.drain(9);
        return Network::FilterStatus::Continue;
      }));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  disconnect();
}

TEST_P(WildcardProxyProtocolTest, BasicV6) {
  connect();
  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> FilterStatus {
        EXPECT_EQ(server_connection_->remoteAddress()->asString(), "[1:2:3::4]:65535");
        EXPECT_EQ(server_connection_->localAddress()->asString(), "[5:6::7:8]:1234");

        EXPECT_EQ(TestUtility::bufferToString(buffer), "more data");
        buffer.drain(9);
        return Network::FilterStatus::Continue;
      }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  disconnect();
}

} // namespace Network
} // namespace Envoy
