#include "common/network/address_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ByRef;
using testing::Eq;
using testing::Invoke;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {

static void errorCallbackTest(Address::IpVersion version) {
  // Force the error callback to fire by closing the socket under the listener. We run this entire
  // test in the forked process to avoid confusion when the fork happens.
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;

  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createListener(connection_handler, socket, listener_callbacks, stats_store,
                                {.bind_to_port_ = true,
                                 .use_proxy_proto_ = false,
                                 .use_original_dst_ = false,
                                 .per_connection_buffer_limit_bytes_ = 0});

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        socket.close();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

class ListenerImplDeathTest : public testing::TestWithParam<Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, ListenerImplDeathTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ListenerImplDeathTest, ErrorCallback) {
  EXPECT_DEATH(errorCallbackTest(GetParam()), ".*listener accept failure.*");
}

class TestListenerImpl : public ListenerImpl {
public:
  TestListenerImpl(Network::ConnectionHandler& conn_handler, Event::DispatcherImpl& dispatcher,
                   ListenSocket& socket, ListenerCallbacks& cb, Stats::Store& stats_store,
                   const Network::ListenerOptions& listener_options)
      : ListenerImpl(conn_handler, dispatcher, socket, cb, stats_store, listener_options) {
    ON_CALL(*this, newConnection(_, _, _, _))
        .WillByDefault(Invoke(
            [this](int fd, Address::InstanceConstSharedPtr remote_address,
                   Address::InstanceConstSharedPtr local_address, bool using_original_dst) -> void {
              ListenerImpl::newConnection(fd, remote_address, local_address, using_original_dst);
            }

            ));
  }

  MOCK_METHOD1(getLocalAddress, Address::InstanceConstSharedPtr(int fd));
  MOCK_METHOD1(getOriginalDst, Address::InstanceConstSharedPtr(int fd));
  MOCK_METHOD4(newConnection,
               void(int fd, Address::InstanceConstSharedPtr remote_address,
                    Address::InstanceConstSharedPtr local_address, bool using_original_dst));
};

class ListenerImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  ListenerImplTest()
      : version_(GetParam()),
        alt_address_(Network::Test::findOrCheckFreePort(
            Network::Test::getCanonicalLoopbackAddress(version_), Address::SocketType::Stream)) {}

  const Address::IpVersion version_;
  const Address::InstanceConstSharedPtr alt_address_;
};
INSTANTIATE_TEST_CASE_P(IpVersions, ListenerImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ListenerImplTest, NormalRedirect) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), true);
  Network::TcpListenSocket socketDst(alt_address_, false);
  Network::MockListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  // The traffic should redirect from binding listener to the virtual listener.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks1,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = true,
                                      .per_connection_buffer_limit_bytes_ = 0});
  Network::MockListenerCallbacks listener_callbacks2;
  Network::TestListenerImpl listenerDst(connection_handler, dispatcher, socketDst,
                                        listener_callbacks2, stats_store,
                                        Network::ListenerOptions());

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).Times(0);
  EXPECT_CALL(listener, getOriginalDst(_)).WillOnce(Return(alt_address_));
  EXPECT_CALL(connection_handler, findListenerByAddress(Eq(ByRef(*alt_address_))))
      .WillOnce(Return(&listenerDst));

  EXPECT_CALL(listener, newConnection(_, _, _, _)).Times(0);
  EXPECT_CALL(listenerDst, newConnection(_, _, _, _));
  EXPECT_CALL(listener_callbacks2, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*alt_address_, *conn->localAddress());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, FallbackToWildcardListener) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), true);
  Network::TcpListenSocket socketDst(Network::Test::getAnyAddress(version_), false);
  Network::MockListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  // The virtual listener of exact address does not exist, fall back to wild card virtual listener.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks1,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = true,
                                      .per_connection_buffer_limit_bytes_ = 0});
  Network::MockListenerCallbacks listener_callbacks2;
  Network::TestListenerImpl listenerDst(connection_handler, dispatcher, socketDst,
                                        listener_callbacks2, stats_store,
                                        Network::ListenerOptions());

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).Times(0);
  EXPECT_CALL(listener, getOriginalDst(_)).WillOnce(Return(alt_address_));
  EXPECT_CALL(connection_handler, findListenerByAddress(Eq(ByRef(*alt_address_))))
      .WillOnce(Return(&listenerDst));

  EXPECT_CALL(listener, newConnection(_, _, _, _)).Times(0);
  EXPECT_CALL(listenerDst, newConnection(_, _, _, _));
  EXPECT_CALL(listener_callbacks2, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*alt_address_, *conn->localAddress());
        EXPECT_FALSE(*socketDst.localAddress() == *conn->localAddress());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, WildcardListenerWithOriginalDst) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getAnyAddress(version_), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  // The virtual listener of exact address does not exist, fall back to the wild card listener.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = true,
                                      .per_connection_buffer_limit_bytes_ = 0});

  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket.localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).WillOnce(Return(local_dst_address));
  EXPECT_CALL(listener, getOriginalDst(_)).WillOnce(Return(alt_address_));
  EXPECT_CALL(connection_handler, findListenerByAddress(Eq(ByRef(*alt_address_))))
      .WillOnce(Return(&listener));

  EXPECT_CALL(listener, newConnection(_, _, _, _));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *alt_address_);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, WildcardListenerNoOriginalDst) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getAnyAddress(version_), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  // The virtual listener of exact address does not exist, fall back to the wild card listener.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = true,
                                      .per_connection_buffer_limit_bytes_ = 0});

  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket.localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).WillOnce(Return(local_dst_address));
  // getOriginalDst() returns the same address as the connections destination.
  EXPECT_CALL(listener, getOriginalDst(_)).WillOnce(Return(local_dst_address));
  EXPECT_CALL(connection_handler, findListenerByAddress(_)).Times(0);

  EXPECT_CALL(listener, newConnection(_, _, _, _));
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, UseActualDst) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version_), true);
  Network::TcpListenSocket socketDst(alt_address_, false);
  Network::MockListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  // Do not redirect since use_original_dst is false.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks1,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = false,
                                      .per_connection_buffer_limit_bytes_ = 0});
  Network::MockListenerCallbacks listener_callbacks2;
  Network::TestListenerImpl listenerDst(connection_handler, dispatcher, socketDst,
                                        listener_callbacks2, stats_store,
                                        Network::ListenerOptions());

  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      socket.localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).Times(0);
  EXPECT_CALL(listener, getOriginalDst(_)).Times(0);
  EXPECT_CALL(connection_handler, findListenerByAddress(_)).Times(0);

  EXPECT_CALL(listener, newConnection(_, _, _, _)).Times(1);
  EXPECT_CALL(listenerDst, newConnection(_, _, _, _)).Times(0);
  EXPECT_CALL(listener_callbacks1, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *socket.localAddress());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(ListenerImplTest, WildcardListenerUseActualDst) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getAnyAddress(version_), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  // Do not redirect since use_original_dst is false.
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks,
                                     stats_store,
                                     {.bind_to_port_ = true,
                                      .use_proxy_proto_ = false,
                                      .use_original_dst_ = false,
                                      .per_connection_buffer_limit_bytes_ = 0});

  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket.localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher.createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket());
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).WillOnce(Return(local_dst_address));
  EXPECT_CALL(listener, getOriginalDst(_)).Times(0);
  EXPECT_CALL(connection_handler, findListenerByAddress(_)).Times(0);

  EXPECT_CALL(listener, newConnection(_, _, _, _)).Times(1);
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // namespace Network
} // namespace Envoy
