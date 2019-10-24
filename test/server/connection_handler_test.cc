#include "envoy/server/active_udp_listener_config.h"
#include "envoy/stats/scope.h"

#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByRef;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace {

class ConnectionHandlerTest : public testing::Test, protected Logger::Loggable<Logger::Id::main> {
public:
  ConnectionHandlerTest()
      : handler_(new ConnectionHandlerImpl(dispatcher_, "test")),
        filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()) {}

  class TestListener : public Network::ListenerConfig {
  public:
    TestListener(ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
                 bool hand_off_restored_destination_connections, const std::string& name,
                 Network::Address::SocketType socket_type,
                 std::chrono::milliseconds listener_filters_timeout,
                 bool continue_on_listener_filters_timeout)
        : parent_(parent), tag_(tag), bind_to_port_(bind_to_port),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          name_(name), listener_filters_timeout_(listener_filters_timeout),
          continue_on_listener_filters_timeout_(continue_on_listener_filters_timeout),
          connection_balancer_(std::make_unique<Network::NopConnectionBalancerImpl>()) {
      envoy::api::v2::listener::UdpListenerConfig dummy;
      std::string listener_name("raw_udp_listener");
      dummy.set_udp_listener_name(listener_name);
      udp_listener_factory_ =
          Config::Utility::getAndCheckFactory<ActiveUdpListenerConfigFactory>(listener_name)
              .createActiveUdpListenerFactory(dummy);
      EXPECT_CALL(socket_, socketType()).WillOnce(Return(socket_type));
    }

    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_.manager_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_.factory_; }
    Network::Socket& socket() override { return socket_; }
    const Network::Socket& socket() const override { return socket_; }
    bool bindToPort() override { return bind_to_port_; }
    bool handOffRestoredDestinationConnections() const override {
      return hand_off_restored_destination_connections_;
    }
    uint32_t perConnectionBufferLimitBytes() const override { return 0; }
    std::chrono::milliseconds listenerFiltersTimeout() const override {
      return listener_filters_timeout_;
    }
    bool continueOnListenerFiltersTimeout() const override {
      return continue_on_listener_filters_timeout_;
    }
    Stats::Scope& listenerScope() override { return parent_.stats_store_; }
    uint64_t listenerTag() const override { return tag_; }
    const std::string& name() const override { return name_; }
    const Network::ActiveUdpListenerFactory* udpListenerFactory() override {
      return udp_listener_factory_.get();
    }
    envoy::api::v2::core::TrafficDirection direction() const override {
      return envoy::api::v2::core::TrafficDirection::UNSPECIFIED;
    }
    Network::ConnectionBalancer& connectionBalancer() override { return *connection_balancer_; }

    ConnectionHandlerTest& parent_;
    Network::MockListenSocket socket_;
    uint64_t tag_;
    bool bind_to_port_;
    const bool hand_off_restored_destination_connections_;
    const std::string name_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const bool continue_on_listener_filters_timeout_;
    std::unique_ptr<Network::ActiveUdpListenerFactory> udp_listener_factory_;
    Network::ConnectionBalancerPtr connection_balancer_;
  };

  using TestListenerPtr = std::unique_ptr<TestListener>;

  TestListener*
  addListener(uint64_t tag, bool bind_to_port, bool hand_off_restored_destination_connections,
              const std::string& name,
              Network::Address::SocketType socket_type = Network::Address::SocketType::Stream,
              std::chrono::milliseconds listener_filters_timeout = std::chrono::milliseconds(15000),
              bool continue_on_listener_filters_timeout = false) {
    listeners_.emplace_back(std::make_unique<TestListener>(
        *this, tag, bind_to_port, hand_off_restored_destination_connections, name, socket_type,
        listener_filters_timeout, continue_on_listener_filters_timeout));
    return listeners_.back().get();
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::list<TestListenerPtr> listeners_;
  Network::ConnectionHandlerPtr handler_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> factory_;
  const Network::FilterChainSharedPtr filter_chain_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
};

// Verify that if a listener is removed while a rebalanced connection is in flight, we correctly
// destroy the connection.
TEST_F(ConnectionHandlerTest, RemoveListenerDuringRebalance) {
  InSequence s;

  // For reasons I did not investigate, the death test below requires this, likely due to forking.
  // So we just leak the FDs for this test.
  ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockConnectionBalancer* connection_balancer = new Network::MockConnectionBalancer();
  test_listener->connection_balancer_.reset(connection_balancer);
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));

  Network::BalancedConnectionHandler* current_handler;
  EXPECT_CALL(*connection_balancer, registerHandler(_)).WillOnce(SaveArgAddress(&current_handler));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  // Fake a balancer posting a connection to us.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  current_handler->incNumConnections();
  current_handler->post(Network::ConnectionSocketPtr{connection});

  EXPECT_CALL(*connection_balancer, unregisterHandler(_));

  // This also tests the assert in ConnectionHandlerImpl::ActiveTcpListener::~ActiveTcpListener.
  // See the long comment at the end of that function.
#ifndef NDEBUG
  // On debug builds this should crash.
  EXPECT_DEATH(handler_->removeListeners(1), ".*num_listener_connections_ == 0.*");
  // The original test continues without the previous line being run. To avoid the same assert
  // firing during teardown, run the posted callback now.
  post_cb();
  EXPECT_CALL(*listener, onDestroy());
#else
  // On release builds this should be fine.
  EXPECT_CALL(*listener, onDestroy());
  handler_->removeListeners(1);
  post_cb();
#endif
}

TEST_F(ConnectionHandlerTest, RemoveListener) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0);
  handler_->removeListeners(0);

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1);

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0);
  handler_->removeListeners(0);
}

TEST_F(ConnectionHandlerTest, DisableListener) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, false, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
}

TEST_F(ConnectionHandlerTest, AddDisabledListener) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, false, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(test_listener->socket_, localAddress());
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
  handler_->addListener(*test_listener);
}

TEST_F(ConnectionHandlerTest, DestroyCloseConnections) {
  InSequence s;

  Network::MockListener* listener = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*listener, onDestroy());
  handler_.reset();
}

TEST_F(ConnectionHandlerTest, CloseDuringFilterChainCreate) {
  InSequence s;

  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*connection, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, CloseConnectionOnEmptyFilterChain) {
  InSequence s;

  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, NormalRedirect) {
  TestListener* test_listener1 = addListener(1, true, true, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  handler_->addListener(*test_listener1);

  TestListener* test_listener2 = addListener(1, false, false, "test_listener2");
  Network::MockListener* listener2 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks2;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks2 = &cb;
            return listener2;
          }));
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(test_listener2->socket_, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  handler_->addListener(*test_listener2);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
          redirected = true;
        }
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accepted_socket, restoreLocalAddress(alt_address));
  EXPECT_CALL(*accepted_socket, localAddressRestored()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  // Verify per-listener connection stats.
  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, FallbackToWildcardListener) {
  TestListener* test_listener1 = addListener(1, true, true, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  handler_->addListener(*test_listener1);

  TestListener* test_listener2 = addListener(1, false, false, "test_listener2");
  Network::MockListener* listener2 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks2;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks2 = &cb;
            return listener2;
          }));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getIpv4AnyAddress();
  EXPECT_CALL(test_listener2->socket_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(*test_listener2);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
          redirected = true;
        }
        return true;
      }));
  // Zero port to match the port of AnyAddress
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 0));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accepted_socket, restoreLocalAddress(alt_address));
  EXPECT_CALL(*accepted_socket, localAddressRestored()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithOriginalDst) {
  TestListener* test_listener1 = addListener(1, true, true, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
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
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(*test_listener1);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*accepted_socket, restoreLocalAddress(original_dst_address));
  EXPECT_CALL(*accepted_socket, localAddressRestored()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, localAddress()).WillRepeatedly(ReturnRef(original_dst_address));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithNoOriginalDst) {
  TestListener* test_listener1 = addListener(1, true, true, "test_listener1");
  Network::MockListener* listener1 = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks1;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks1 = &cb;
            return listener1;
          }));
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  EXPECT_CALL(test_listener1->socket_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(*test_listener1);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*accepted_socket, localAddressRestored()).WillOnce(Return(false));
  EXPECT_CALL(*accepted_socket, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, TransportProtocolDefault) {
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*accepted_socket, detectedTransportProtocol())
      .WillOnce(Return(absl::string_view("")));
  EXPECT_CALL(*accepted_socket, setDetectedTransportProtocol(absl::string_view("raw_buffer")));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TransportProtocolCustom) {
  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  absl::string_view dummy = "dummy";
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().setDetectedTransportProtocol(dummy);
        return Network::FilterStatus::Continue;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*accepted_socket, setDetectedTransportProtocol(dummy));
  EXPECT_CALL(*accepted_socket, detectedTransportProtocol()).WillOnce(Return(dummy));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

// Timeout during listener filter stop iteration.
TEST_F(ConnectionHandlerTest, ListenerFilterTimeout) {
  InSequence s;

  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  Network::IoSocketHandleImpl io_handle{42};
  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  Stats::Gauge& downstream_pre_cx_active =
      stats_store_.gauge("downstream_pre_cx_active", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1UL, downstream_pre_cx_active.value());

  EXPECT_CALL(*timeout, disableTimer());
  timeout->invokeCallback();
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, downstream_pre_cx_active.value());
  EXPECT_EQ(1UL, stats_store_.counter("downstream_pre_cx_timeout").value());

  // Make sure we didn't continue to try create connection.
  EXPECT_EQ(0UL, stats_store_.counter("no_filter_chain_match").value());

  EXPECT_CALL(*listener, onDestroy());
}

// Continue on timeout during listener filter stop iteration.
TEST_F(ConnectionHandlerTest, ContinueOnListenerFilterTimeout) {
  InSequence s;

  TestListener* test_listener =
      addListener(1, true, false, "test_listener", Network::Address::SocketType::Stream,
                  std::chrono::milliseconds(15000), true);
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  Network::IoSocketHandleImpl io_handle{42};
  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  Stats::Gauge& downstream_pre_cx_active =
      stats_store_.gauge("downstream_pre_cx_active", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1UL, downstream_pre_cx_active.value());

  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(*timeout, disableTimer());
  timeout->invokeCallback();
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, downstream_pre_cx_active.value());
  EXPECT_EQ(1UL, stats_store_.counter("downstream_pre_cx_timeout").value());

  // Make sure we continued to try create connection.
  EXPECT_EQ(1UL, stats_store_.counter("no_filter_chain_match").value());

  EXPECT_CALL(*listener, onDestroy());
}

// Timeout is disabled once the listener filters complete.
TEST_F(ConnectionHandlerTest, ListenerFilterTimeoutResetOnSuccess) {
  InSequence s;

  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  Network::ListenerFilterCallbacks* listener_filter_cb{};
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        listener_filter_cb = &cb;
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  Network::IoSocketHandleImpl io_handle{42};
  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle));

  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(*timeout, disableTimer());
  listener_filter_cb->continueFilterChain(true);

  EXPECT_CALL(*listener, onDestroy());
}

// Ensure there is no timeout when the timeout is disabled with 0s.
TEST_F(ConnectionHandlerTest, ListenerFilterDisabledTimeout) {
  InSequence s;

  TestListener* test_listener =
      addListener(1, true, false, "test_listener", Network::Address::SocketType::Stream,
                  std::chrono::milliseconds());
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

// Listener Filter could close socket in the context of listener callback.
TEST_F(ConnectionHandlerTest, ListenerFilterReportError) {
  InSequence s;

  TestListener* test_listener = addListener(1, true, false, "test_listener");
  Network::MockListener* listener = new Network::MockListener();
  Network::ListenerCallbacks* listener_callbacks;
  EXPECT_CALL(dispatcher_, createListener_(_, _, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::ListenerCallbacks& cb, bool) -> Network::Listener* {
            listener_callbacks = &cb;
            return listener;
          }));
  EXPECT_CALL(test_listener->socket_, localAddress());
  handler_->addListener(*test_listener);

  Network::MockListenerFilter* first_filter = new Network::MockListenerFilter();
  Network::MockListenerFilter* last_filter = new Network::MockListenerFilter();

  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(Network::ListenerFilterPtr{first_filter});
        manager.addAcceptFilter(Network::ListenerFilterPtr{last_filter});
        return true;
      }));
  // The first filter close the socket
  EXPECT_CALL(*first_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().close();
        return Network::FilterStatus::StopIteration;
      }));
  // The last filter won't be invoked
  EXPECT_CALL(*last_filter, onAccept(_)).Times(0);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  dispatcher_.clearDeferredDeleteList();
  // Make sure the error leads to no listener timer created.
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  // Make sure we never try to match the filer chain since listener filter doesn't complete.
  EXPECT_CALL(manager_, findFilterChain(_)).Times(0);

  EXPECT_CALL(*listener, onDestroy());
}

// Ensure an exception is thrown if there are no filters registered for a UDP listener
TEST_F(ConnectionHandlerTest, UdpListenerNoFilterThrowsException) {
  InSequence s;

  TestListener* test_listener =
      addListener(1, true, false, "test_listener", Network::Address::SocketType::Datagram,
                  std::chrono::milliseconds());
  Network::MockUdpListener* listener = new Network::MockUdpListener();
  EXPECT_CALL(dispatcher_, createUdpListener_(_, _))
      .WillOnce(
          Invoke([&](Network::Socket&, Network::UdpListenerCallbacks&) -> Network::UdpListener* {
            return listener;
          }));
  EXPECT_CALL(factory_, createUdpListenerFilterChain(_, _))
      .WillOnce(Invoke([&](Network::UdpListenerFilterManager&,
                           Network::UdpReadFilterCallbacks&) -> bool { return true; }));
  EXPECT_CALL(*listener, onDestroy());

  try {
    handler_->addListener(*test_listener);
    FAIL();
  } catch (const Network::CreateListenerException& e) {
    EXPECT_THAT(
        e.what(),
        HasSubstr("Cannot create listener as no read filter registered for the udp listener"));
  }
}

} // namespace
} // namespace Server
} // namespace Envoy
