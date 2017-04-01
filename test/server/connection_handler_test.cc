#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

using testing::_;
using testing::ByRef;
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
  Server::ConnectionHandlerImpl handler(log(), Api::ApiPtr{api});
  Network::MockFilterChainFactory factory;
  Network::MockConnectionHandler connection_handler;
  NiceMock<Network::MockListenSocket> socket;

  Network::Listener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(*dispatcher, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler.addListener(factory, socket, stats_store,
                      Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory, createFilterChain(_));
  EXPECT_CALL(*connection, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler.numConnections());
}

TEST_F(ConnectionHandlerTest, CloseConnectionOnEmptyFilterChain) {
  InSequence s;

  Stats::IsolatedStoreImpl stats_store;
  Api::MockApi* api = new Api::MockApi();
  Event::MockDispatcher* dispatcher = new NiceMock<Event::MockDispatcher>();
  EXPECT_CALL(*api, allocateDispatcher_()).WillOnce(Return(dispatcher));
  Server::ConnectionHandlerImpl handler(log(), Api::ApiPtr{api});
  Network::MockFilterChainFactory factory;
  Network::MockConnectionHandler connection_handler;
  NiceMock<Network::MockListenSocket> socket;

  Network::Listener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(*dispatcher, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler.addListener(factory, socket, stats_store,
                      Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory, createFilterChain(_)).WillOnce(Return(false));
  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler.numConnections());
}

TEST_F(ConnectionHandlerTest, FindListenerByAddress) {
  Stats::IsolatedStoreImpl stats_store;
  Api::MockApi* api = new Api::MockApi();
  Event::MockDispatcher* dispatcher = new NiceMock<Event::MockDispatcher>();
  EXPECT_CALL(*api, allocateDispatcher_()).WillOnce(Return(dispatcher));
  Server::ConnectionHandlerImpl handler(log(), Api::ApiPtr{api});
  Network::MockFilterChainFactory factory;
  Network::MockConnectionHandler connection_handler;
  Network::MockListenSocket socket;
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(socket, localAddress()).WillRepeatedly(Return(alt_address));

  Network::Listener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(*dispatcher, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler.addListener(factory, socket, stats_store,
                      Network::ListenerOptions::listenerOptionsWithBindToPort());

  EXPECT_EQ(listener, handler.findListenerByAddress(ByRef(*alt_address)));

  Network::MockListenSocket socket2;
  Network::Address::InstanceConstSharedPtr alt_address2(
      new Network::Address::Ipv4Instance("0.0.0.0", 10001));
  Network::Address::InstanceConstSharedPtr alt_address3(
      new Network::Address::Ipv4Instance("127.0.0.2", 10001));
  EXPECT_CALL(socket2, localAddress()).WillRepeatedly(Return(alt_address2));

  Network::Listener* listener2 = new Network::MockListener();
  EXPECT_CALL(*dispatcher, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener2;

      }));
  handler.addListener(factory, socket2, stats_store,
                      Network::ListenerOptions::listenerOptionsWithBindToPort());

  EXPECT_EQ(listener, handler.findListenerByAddress(ByRef(*alt_address)));
  EXPECT_EQ(listener2, handler.findListenerByAddress(ByRef(*alt_address2)));
  EXPECT_EQ(listener2, handler.findListenerByAddress(ByRef(*alt_address3)));
}
