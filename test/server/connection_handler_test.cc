#include "common/stats/stats_impl.h"
#include "server/connection_handler.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

class ConnectionHandlerTest : public testing::Test, protected Logger::Loggable<Logger::Id::main> {};

TEST_F(ConnectionHandlerTest, CloseDuringFilterChainCreate) {
  InSequence s;

  Stats::IsolatedStoreImpl stats_store;
  Api::MockApi* api = new Api::MockApi();
  Event::MockDispatcher* dispatcher = new NiceMock<Event::MockDispatcher>();
  EXPECT_CALL(*api, allocateDispatcher_()).WillOnce(Return(dispatcher));
  ConnectionHandler handler(stats_store, log(), Api::ApiPtr{api});
  Network::MockFilterChainFactory factory;
  NiceMock<Network::MockListenSocket> socket;

  Network::Listener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(*dispatcher, createListener_(_, _, _, _, _, _))
      .WillOnce(Invoke([&](Network::ListenSocket&, Network::ListenerCallbacks& cb, Stats::Store&,
                           bool, bool, bool) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler.addListener(factory, socket, true, false, false);

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory, createFilterChain(_));
  EXPECT_CALL(*connection, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler.numConnections());
}
