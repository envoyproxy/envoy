#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Network {

class ProxyProtocolTest : public testing::TestWithParam<Address::IpVersion> {
public:
  ProxyProtocolTest()
      : socket_(Network::Test::getCanonicalLoopbackAddress(GetParam()), true),
        listener_(connection_handler_, dispatcher_, socket_, callbacks_, stats_store_,
                  {.bind_to_port_ = true,
                   .use_proxy_proto_ = true,
                   .use_original_dst_ = false,
                   .per_connection_buffer_limit_bytes_ = 0}) {
    conn_ = dispatcher_.createClientConnection(socket_.localAddress());
    conn_->addConnectionCallbacks(connection_callbacks_);
    conn_->connect();
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf);
  }

  Event::DispatcherImpl dispatcher_;
  TcpListenSocket socket_;
  Stats::IsolatedStoreImpl stats_store_;
  MockListenerCallbacks callbacks_;
  Network::MockConnectionHandler connection_handler_;
  ListenerImpl listener_;
  ClientConnectionPtr conn_;
  NiceMock<MockConnectionCallbacks> connection_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, ProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ProxyProtocolTest, Basic) {

  write("PROXY TCP4 1.2.3.4 255.255.255.255 65535 1234\r\nmore data");

  ConnectionPtr accepted_connection;

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("1.2.3.4", conn->remoteAddress().ip()->addressAsString());
    conn->addReadFilter(read_filter_);
    accepted_connection = std::move(conn);
  }));

  read_filter_.reset(new MockReadFilter());
  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(BufferStringEqual("more data")));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  accepted_connection->close(ConnectionCloseType::NoFlush);
  conn_->close(ConnectionCloseType::NoFlush);
}

TEST_P(ProxyProtocolTest, BasicV6) {

  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  ConnectionPtr accepted_connection;

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("1:2:3::4", conn->remoteAddress().ip()->addressAsString());
    conn->addReadFilter(read_filter_);
    accepted_connection = std::move(conn);
  }));

  read_filter_.reset(new MockReadFilter());
  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(BufferStringEqual("more data")));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  accepted_connection->close(ConnectionCloseType::NoFlush);
  conn_->close(ConnectionCloseType::NoFlush);
}

TEST_P(ProxyProtocolTest, Fragmented) {

  write("PROXY TCP4");
  write(" 255.255.2");
  write("55.255 1.2");
  write(".3.4 65535");
  write(" 1234\r\n");

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("255.255.255.255", conn->remoteAddress().ip()->addressAsString());
    read_filter_.reset(new MockReadFilter());
    conn->addReadFilter(read_filter_);
    conn->close(ConnectionCloseType::NoFlush);
  }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, PartialRead) {

  write("PROXY TCP4");
  write(" 255.255.2");

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("255.255.255.255", conn->remoteAddress().ip()->addressAsString());
    read_filter_.reset(new MockReadFilter());
    conn->addReadFilter(read_filter_);
    conn->close(ConnectionCloseType::NoFlush);
  }));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);

  write("55.255 1.2");
  write(".3.4 65535");
  write(" 1234\r\n");

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, MalformedProxyLine) {
  write("BOGUS\r\n");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, ProxyLineTooLarge) {
  write("012345678901234567890123456789012345678901234567890123456789"
        "012345678901234567890123456789012345678901234567890123456789");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, NotEnoughFields) {
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, BadPort) {
  write("PROXY TCP6 1:2:3::4 5:6::7:8 1234 abc\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, NegativePort) {
  write("PROXY TCP6 1:2:3::4 5:6::7:8 -1 1234\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, PortOutOfRange) {
  write("PROXY TCP6 1:2:3::4 5:6::7:8 66776 1234\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, BadAddress) {
  write("PROXY TCP6 1::2:3::4 5:6::7:8 1234 5678\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

TEST_P(ProxyProtocolTest, AddressVersionsNotMatch) {
  write("PROXY TCP6 1:2:3::4 1.2.3.4 1234 5678\r\nmore data");
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::Connected));
  EXPECT_CALL(connection_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
}

class WildcardProxyProtocolTest : public testing::TestWithParam<Address::IpVersion> {
public:
  WildcardProxyProtocolTest()
      : socket_(Network::Test::getAnyAddress(GetParam()), true),
        local_dst_address_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(GetParam()),
            socket_.localAddress()->ip()->port())),
        listener_(connection_handler_, dispatcher_, socket_, callbacks_, stats_store_,
                  {.bind_to_port_ = true,
                   .use_proxy_proto_ = true,
                   .use_original_dst_ = false,
                   .per_connection_buffer_limit_bytes_ = 0}) {
    conn_ = dispatcher_.createClientConnection(local_dst_address_);
    conn_->addConnectionCallbacks(connection_callbacks_);
    conn_->connect();
  }

  void write(const std::string& s) {
    Buffer::OwnedImpl buf(s);
    conn_->write(buf);
  }

  Event::DispatcherImpl dispatcher_;
  TcpListenSocket socket_;
  Network::Address::InstanceConstSharedPtr local_dst_address_;
  Stats::IsolatedStoreImpl stats_store_;
  MockListenerCallbacks callbacks_;
  Network::MockConnectionHandler connection_handler_;
  ListenerImpl listener_;
  ClientConnectionPtr conn_;
  NiceMock<MockConnectionCallbacks> connection_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
};

// Parameterize the listener socket address version.
INSTANTIATE_TEST_CASE_P(IpVersions, WildcardProxyProtocolTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(WildcardProxyProtocolTest, Basic) {

  write("PROXY TCP4 1.2.3.4 255.255.255.255 65535 1234\r\nmore data");

  ConnectionPtr accepted_connection;

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("1.2.3.4", conn->remoteAddress().ip()->addressAsString());
    EXPECT_EQ(conn->localAddress(), *local_dst_address_);
    conn->addReadFilter(read_filter_);
    accepted_connection = std::move(conn);
  }));

  read_filter_.reset(new MockReadFilter());
  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(BufferStringEqual("more data")));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  accepted_connection->close(ConnectionCloseType::NoFlush);
  conn_->close(ConnectionCloseType::NoFlush);
}

TEST_P(WildcardProxyProtocolTest, BasicV6) {

  write("PROXY TCP6 1:2:3::4 5:6::7:8 65535 1234\r\nmore data");

  ConnectionPtr accepted_connection;

  EXPECT_CALL(callbacks_, onNewConnection_(_)).WillOnce(Invoke([&](ConnectionPtr& conn) -> void {
    ASSERT_EQ("1:2:3::4", conn->remoteAddress().ip()->addressAsString());
    EXPECT_EQ(conn->localAddress(), *local_dst_address_);
    conn->addReadFilter(read_filter_);
    accepted_connection = std::move(conn);
  }));

  read_filter_.reset(new MockReadFilter());
  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(BufferStringEqual("more data")));

  dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  accepted_connection->close(ConnectionCloseType::NoFlush);
  conn_->close(ConnectionCloseType::NoFlush);
}

} // namespace Network
} // namespace Envoy
