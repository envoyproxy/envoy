#include "common/network/listener_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

using testing::_;
using testing::Invoke;

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

} // Network
