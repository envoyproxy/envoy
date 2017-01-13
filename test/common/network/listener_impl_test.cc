#include "common/network/listener_impl.h"
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
      dispatcher.createClientConnection("tcp://127.0.0.1:10000");
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
                     use_proxy_proto, use_orig_dst) {}
  ~TestListenerImpl() {}

  MOCK_METHOD1(getAddressPort_, uint16_t(sockaddr*));
  MOCK_METHOD2(newConnection_, void(int, sockaddr*));

  void newConnection(int fd, sockaddr* addr) override { newConnection_(fd, addr); }
  void newConnection(int fd, const std::string& addr) override {
    ListenerImpl::newConnection(fd, addr);
  }

protected:
  uint16_t getAddressPort(sockaddr* sock) override { return getAddressPort_(sock); }
};

static void useOriginalDst() {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(uint32_t(10000), true);
  Network::TcpListenSocket socketDst(uint32_t(10001), false);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::TestListenerImpl listener(connection_handler, dispatcher, socket, listener_callbacks,
                                     stats_store, true, false, true);
  Network::TestListenerImpl listenerDst(connection_handler, dispatcher, socketDst,
                                        listener_callbacks, stats_store, false, false, false);

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection("tcp://127.0.0.1:10000");
  client_connection->connect();

  EXPECT_CALL(listener, getAddressPort_(_)).WillRepeatedly(Return(10001));
  EXPECT_CALL(connection_handler, findListener("10001")).WillRepeatedly(Return(&listenerDst));

  EXPECT_CALL(listener, newConnection_(_, _)).Times(0);
  EXPECT_CALL(listenerDst, newConnection_(_, _))
      .Times(1)
      .WillOnce(Invoke([&](int, sockaddr*) -> void {
        client_connection->close(ConnectionCloseType::NoFlush);
        socket.close();
        socketDst.close();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(ListenerImpl, UseOriginalDst) {
  EXPECT_DEATH(useOriginalDst(), ".*listener accept failure.*");
}

} // Network
