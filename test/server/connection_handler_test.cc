#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::A;
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

  // Listener
  class TestListener : public Listener, public LinkedObject<TestListener> {
  public:
    TestListener(ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
                 bool use_original_dst, const std::string& name)
        : parent_(parent), tag_(tag), bind_to_port_(bind_to_port),
          use_original_dst_(use_original_dst), name_(name) {}

    Network::FilterChainFactory& filterChainFactory() override { return parent_.factory_; }
    Network::ListenSocket& socket() override { return socket_; }
    Ssl::ServerContext* defaultSslContext() override { return nullptr; }
    bool useProxyProto() override { return false; }
    bool bindToPort() override { return bind_to_port_; }
    bool useOriginalDst() override { return use_original_dst_; }
    uint32_t perConnectionBufferLimitBytes() override { return 0; }
    Stats::Scope& listenerScope() override { return parent_.stats_store_; }
    uint64_t listenerTag() const override { return tag_; }
    const std::string& name() const override { return name_; }

    ConnectionHandlerTest& parent_;
    Network::MockListenSocket socket_;
    uint64_t tag_;
    bool bind_to_port_;
    bool use_original_dst_;
    const std::string name_;
  };

  typedef std::unique_ptr<TestListener> TestListenerPtr;

  TestListener* addListener(uint64_t tag, bool bind_to_port, bool use_original_dst,
                            const std::string& name) {
    TestListener* listener = new TestListener(*this, tag, bind_to_port, use_original_dst, name);
    listener->moveIntoListBack(TestListenerPtr{listener}, listeners_);
    return listener;
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  NiceMock<Network::MockFilterChainFactory> factory_;
  std::list<TestListenerPtr> listeners_;
};

TEST_F(ConnectionHandlerTest, RemoveListener) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;

          }));
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
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
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;

          }));
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
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
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;

          }));
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>()));
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
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;

          }));
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(false));
  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  listener_callbacks->onNewConnection(Network::ConnectionPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, FindListenerByAddress) {
  TestListener* test_listener1 = addListener(1, true, false, "test_listener1");
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));

  Network::MockListener* listener = new Network::MockListener();
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke([&](Network::ListenSocket&, Network::ListenerCallbacks&,
                           bool) -> Network::Listener* { return listener; }));
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(Return(alt_address));
  handler_->addListener(*test_listener1);

  EXPECT_EQ(listener, handler_->findListenerByAddress(ByRef(*alt_address)));

  TestListener* test_listener2 = addListener(2, true, false, "test_listener2");
  Network::Address::InstanceConstSharedPtr alt_address2(
      new Network::Address::Ipv4Instance("0.0.0.0", 10001));
  Network::Address::InstanceConstSharedPtr alt_address3(
      new Network::Address::Ipv4Instance("127.0.0.2", 10001));

  Network::MockListener* listener2 = new Network::MockListener();
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke([&](Network::ListenSocket&, Network::ListenerCallbacks&,
                           bool) -> Network::Listener* { return listener2; }));
  EXPECT_CALL(test_listener2->socket_, localAddress()).WillRepeatedly(Return(alt_address2));
  handler_->addListener(*test_listener2);

  EXPECT_EQ(listener, handler_->findListenerByAddress(ByRef(*alt_address)));
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address2)));
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address3)));

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1);
  EXPECT_EQ(listener2, handler_->findListenerByAddress(ByRef(*alt_address)));

  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2);

  Network::MockListener* listener3 = new Network::MockListener();
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke([&](Network::ListenSocket&, Network::ListenerCallbacks&,
                           bool) -> Network::Listener* { return listener3; }));
  handler_->addListener(*test_listener2);

  EXPECT_EQ(listener3, handler_->findListenerByAddress(ByRef(*alt_address2)));
  EXPECT_EQ(listener3, handler_->findListenerByAddress(ByRef(*alt_address3)));

  EXPECT_CALL(*listener3, onDestroy());
}

class TestFilter : public Network::ListenerFilter {
public:
  MOCK_METHOD1(onAccept, Network::FilterStatus(Network::ListenerFilterCallbacks&));
};

TEST_F(ConnectionHandlerTest, NormalRedirect) {
  // 'false' for use_original_dst since we add a mock filter manually below.
  TestListener* test_listener1 = addListener(1, true, false, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(Return(normal_address));
  handler_->addListener(*test_listener1);

  TestListener* test_listener2 = addListener(1, false, false, "test_listener2");
  Network::MockListener* listener2 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks2;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks2 = &cb;
            return listener2;
          }));
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(test_listener2->socket_, localAddress()).WillRepeatedly(Return(alt_address));
  handler_->addListener(*test_listener2);

  TestFilter* test_filter = new TestFilter();
  Network::MockAcceptSocket* accept_socket = new NiceMock<Network::MockAcceptSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createFilterChain(A<Network::ListenerFilterManager&>()))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(Network::ListenerFilterSharedPtr{test_filter});
          redirected = true;
        }
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().resetLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accept_socket, resetLocalAddress(alt_address));
  EXPECT_CALL(*accept_socket, localAddressReset()).WillOnce(Return(true));
  EXPECT_CALL(*accept_socket, localAddress()).WillRepeatedly(Return(alt_address));
  EXPECT_CALL(*accept_socket, clearReset());
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createConnection_(_, _)).WillOnce(Return(connection));
  listener_callbacks1->onAccept(Network::AcceptSocketPtr{accept_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, FallbackToWildcardListener) {
  // 'false' for use_original_dst since we add a mock filter manually below.
  TestListener* test_listener1 = addListener(1, true, false, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(Return(normal_address));
  handler_->addListener(*test_listener1);

  TestListener* test_listener2 = addListener(1, false, false, "test_listener2");
  Network::MockListener* listener2 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks2;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks2 = &cb;
            return listener2;
          }));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getIpv4AnyAddress();
  EXPECT_CALL(test_listener2->socket_, localAddress()).WillRepeatedly(Return(any_address));
  handler_->addListener(*test_listener2);

  TestFilter* test_filter = new TestFilter();
  Network::MockAcceptSocket* accept_socket = new NiceMock<Network::MockAcceptSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createFilterChain(A<Network::ListenerFilterManager&>()))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(Network::ListenerFilterSharedPtr{test_filter});
          redirected = true;
        }
        return true;
      }));
  // Zero port to match the port of AnyAddress
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 0));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().resetLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accept_socket, resetLocalAddress(alt_address));
  EXPECT_CALL(*accept_socket, localAddressReset()).WillOnce(Return(true));
  EXPECT_CALL(*accept_socket, localAddress()).WillRepeatedly(Return(alt_address));
  EXPECT_CALL(*accept_socket, clearReset());
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createConnection_(_, _)).WillOnce(Return(connection));
  listener_callbacks1->onAccept(Network::AcceptSocketPtr{accept_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithOriginalDst) {
  // 'false' for use_original_dst since we add a mock filter manually below.
  TestListener* test_listener1 = addListener(1, true, false, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  // Original dst address nor port number match that of the listener's address.
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(Return(any_address));
  handler_->addListener(*test_listener1);

  TestFilter* test_filter = new TestFilter();
  Network::MockAcceptSocket* accept_socket = new NiceMock<Network::MockAcceptSocket>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::ListenerFilterManager&>()))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(Network::ListenerFilterSharedPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().resetLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accept_socket, resetLocalAddress(original_dst_address));
  EXPECT_CALL(*accept_socket, localAddressReset()).WillOnce(Return(true));
  EXPECT_CALL(*accept_socket, localAddress()).WillRepeatedly(Return(original_dst_address));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createConnection_(_, _)).WillOnce(Return(connection));
  listener_callbacks1->onAccept(Network::AcceptSocketPtr{accept_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithNoOriginalDst) {
  // 'false' for use_original_dst since we add a mock filter manually below.
  TestListener* test_listener1 = addListener(1, true, false, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(Invoke(
          [&](Network::ListenSocket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(Return(any_address));
  handler_->addListener(*test_listener1);

  TestFilter* test_filter = new TestFilter();
  Network::MockAcceptSocket* accept_socket = new NiceMock<Network::MockAcceptSocket>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::ListenerFilterManager&>()))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(Network::ListenerFilterSharedPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*accept_socket, localAddressReset()).WillOnce(Return(false));
  EXPECT_CALL(*accept_socket, localAddress()).WillRepeatedly(Return(normal_address));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(factory_, createFilterChain(A<Network::Connection&>())).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createConnection_(_, _)).WillOnce(Return(connection));
  listener_callbacks1->onAccept(Network::AcceptSocketPtr{accept_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
}

} // namespace Server
} // namespace Envoy
