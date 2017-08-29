#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ByRef;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {

class ConnectionHandlerTest : public testing::Test, protected Logger::Loggable<Logger::Id::main> {
public:
  ConnectionHandlerTest() : handler_(new ConnectionHandlerImpl(ENVOY_LOGGER(), dispatcher_)) {}

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  Network::MockFilterChainFactory factory_;
  NiceMock<Network::MockListenSocket> socket_;
};

TEST_F(ConnectionHandlerTest, RemoveListener) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler_->addListener(factory_, socket_, stats_store_, 1,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(_)).WillOnce(Return(true));
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(1UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0);
  handler_->removeListeners(0);

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1);

  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0);
  handler_->removeListeners(0);
}

TEST_F(ConnectionHandlerTest, DestroyCloseConnections) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler_->addListener(factory_, socket_, stats_store_, 1,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(_)).WillOnce(Return(true));
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*listener, onDestroy());
  handler_.reset();
}

TEST_F(ConnectionHandlerTest, CloseDuringFilterChainCreate) {
  InSequence s;

  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler_->addListener(factory_, socket_, stats_store_, 1,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(_));
  EXPECT_CALL(*connection, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, CloseConnectionOnEmptyFilterChain) {
  InSequence s;

  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler_->addListener(factory_, socket_, stats_store_, 1,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(_)).WillOnce(Return(false));
  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, FindListenerByAddress) {
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(socket_, localAddress()).WillRepeatedly(Return(alt_address));

  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener;

      }));
  handler_->addListener(factory_, socket_, stats_store_, 1,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  EXPECT_EQ(listener, handler_->findListenerByAddress(ByRef(*alt_address)));

  Network::MockListenSocket socket2;
  Network::Address::InstanceConstSharedPtr alt_address2(
      new Network::Address::Ipv4Instance("0.0.0.0", 10001));
  Network::Address::InstanceConstSharedPtr alt_address3(
      new Network::Address::Ipv4Instance("127.0.0.2", 10001));
  EXPECT_CALL(socket2, localAddress()).WillRepeatedly(Return(alt_address2));

  Network::MockListener* listener2 = new Network::MockListener();
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener2;

      }));
  handler_->addListener(factory_, socket2, stats_store_, 2,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  EXPECT_EQ(listener, handler_->findListenerByAddress(ByRef(*alt_address)));
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address2)));
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address3)));

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1);
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address)));

  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2);

  Network::MockListener* listener3 = new Network::MockListener();
  EXPECT_CALL(dispatcher_, createListener_(_, _, _, _, _))
      .WillOnce(Invoke([&](Network::ConnectionHandler&, Network::ListenSocket&,
                           Network::ListenerCallbacks& cb, Stats::Scope&,
                           const Network::ListenerOptions&) -> Network::Listener* {
        listener_callbacks = &cb;
        return listener3;

      }));
  handler_->addListener(factory_, socket2, stats_store_, 2,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  EXPECT_EQ(listener3, handler_->findListenerByAddress(ByRef(*alt_address2)));
  EXPECT_EQ(listener3, handler_->findListenerByAddress(ByRef(*alt_address3)));

  EXPECT_CALL(*listener3, onDestroy());
}

} // namespace Server
} // namespace Envoy
