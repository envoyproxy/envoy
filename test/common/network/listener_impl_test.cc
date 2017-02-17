#include "common/network/listener_impl.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Network {

static void errorCallbackTest() {
  // Force the error callback to fire by closing the socket under the listener. We run this entire
  // test in the forked process to avoid confusion when the fork happens.
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(uint32_t(10000), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher.createListener(
      connection_handler, socket, listener_callbacks, stats_store, true, false, false);

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection(Utility::resolveUrl("tcp://127.0.0.1:10000"));
  client_connection->connect();

  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        socket.close();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(ListenerImplDeathTest, ErrorCallback) {
  EXPECT_DEATH(errorCallbackTest(), ".*listener accept failure.*");
}

class TestListenerImpl : public ListenerImpl {
public:
  TestListenerImpl(Network::ConnectionHandler& conn_handler, Event::DispatcherImpl& dispatcher,
                   ListenSocket& socket, ListenerCallbacks& cb, Stats::Store& stats_store,
                   bool bind_to_port, bool use_proxy_proto, bool use_orig_dst)
      : ListenerImpl(conn_handler, dispatcher, socket, cb, stats_store, bind_to_port,
                     use_proxy_proto, use_orig_dst) {
    ON_CALL(*this, newConnection(_, _, _))
        .WillByDefault(Invoke(
            [this](int fd, Address::InstancePtr remote_address, Address::InstancePtr local_address)
                -> void { ListenerImpl::newConnection(fd, remote_address, local_address); }

            ));
  }

  MOCK_METHOD1(getOriginalDst, Address::InstancePtr(int fd));
  MOCK_METHOD3(newConnection, void(int fd, Address::InstancePtr remote_address,
                                   Address::InstancePtr local_address));
};

TEST(ListenerImplTest, UseOriginalDst) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(uint32_t(10000), true);
  Network::TcpListenSocket socketDst(uint32_t(10001), false);
  Network::MockListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks1,
                                     stats_store, true, false, true);
  Network::MockListenerCallbacks listener_callbacks2;
  Network::TestListenerImpl listenerDst(connection_handler, dispatcher, socketDst,
                                        listener_callbacks2, stats_store, false, false, false);

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection(Utility::resolveUrl("tcp://127.0.0.1:10000"));
  client_connection->connect();

  Address::InstancePtr alt_address(new Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(listener, getOriginalDst(_)).WillRepeatedly(Return(alt_address));
  EXPECT_CALL(connection_handler, findListenerByPort(10001)).WillRepeatedly(Return(&listenerDst));

  EXPECT_CALL(listener, newConnection(_, _, _)).Times(0);
  EXPECT_CALL(listenerDst, newConnection(_, _, _));
  EXPECT_CALL(listener_callbacks2, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        EXPECT_EQ("127.0.0.1:10001", conn->localAddress().asString());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Network
