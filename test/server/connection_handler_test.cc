#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/udp_listener_config.pb.h"
#include "envoy/network/exception.h"
#include "envoy/network/filter.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/stats/scope.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/network/address_impl.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

#include "server/connection_handler_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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
      : socket_factory_(std::make_shared<Network::MockListenSocketFactory>()),
        handler_(new ConnectionHandlerImpl(dispatcher_)),
        filter_chain_(Network::Test::createEmptyFilterChainWithRawBufferSockets()),
        listener_filter_matcher_(std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>()) {
    ON_CALL(*listener_filter_matcher_, matches(_)).WillByDefault(Return(false));
  }

  class TestListener : public Network::ListenerConfig {
  public:
    TestListener(
        ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
        bool hand_off_restored_destination_connections, const std::string& name,
        Network::Socket::Type socket_type, std::chrono::milliseconds listener_filters_timeout,
        bool continue_on_listener_filters_timeout,
        Network::ListenSocketFactorySharedPtr socket_factory,
        std::shared_ptr<NiceMock<Network::MockFilterChainManager>> filter_chain_manager = nullptr)
        : parent_(parent), socket_(std::make_shared<Network::MockListenSocket>()),
          socket_factory_(std::move(socket_factory)), tag_(tag), bind_to_port_(bind_to_port),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          name_(name), listener_filters_timeout_(listener_filters_timeout),
          continue_on_listener_filters_timeout_(continue_on_listener_filters_timeout),
          connection_balancer_(std::make_unique<Network::NopConnectionBalancerImpl>()),
          inline_filter_chain_manager_(filter_chain_manager) {
      envoy::config::listener::v3::UdpListenerConfig dummy;
      std::string listener_name("raw_udp_listener");
      dummy.set_udp_listener_name(listener_name);
      udp_listener_factory_ =
          Config::Utility::getAndCheckFactoryByName<ActiveUdpListenerConfigFactory>(listener_name)
              .createActiveUdpListenerFactory(dummy, /*concurrency=*/1);
      ON_CALL(*socket_, socketType()).WillByDefault(Return(socket_type));
    }

    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override {
      return inline_filter_chain_manager_ == nullptr ? parent_.manager_
                                                     : *inline_filter_chain_manager_;
    }
    Network::FilterChainFactory& filterChainFactory() override { return parent_.factory_; }
    Network::ListenSocketFactory& listenSocketFactory() override { return *socket_factory_; }
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
    Network::ActiveUdpListenerFactory* udpListenerFactory() override {
      return udp_listener_factory_.get();
    }
    envoy::config::core::v3::TrafficDirection direction() const override {
      return envoy::config::core::v3::UNSPECIFIED;
    }
    Network::ConnectionBalancer& connectionBalancer() override { return *connection_balancer_; }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return empty_access_logs_;
    }
    ResourceLimit& openConnections() override { return open_connections_; }

    void setMaxConnections(const uint32_t num_connections) {
      open_connections_.setMax(num_connections);
    }
    void clearMaxConnections() { open_connections_.resetMax(); }

    ConnectionHandlerTest& parent_;
    std::shared_ptr<Network::MockListenSocket> socket_;
    Network::ListenSocketFactorySharedPtr socket_factory_;
    uint64_t tag_;
    bool bind_to_port_;
    const bool hand_off_restored_destination_connections_;
    const std::string name_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const bool continue_on_listener_filters_timeout_;
    std::unique_ptr<Network::ActiveUdpListenerFactory> udp_listener_factory_;
    Network::ConnectionBalancerPtr connection_balancer_;
    BasicResourceLimitImpl open_connections_;
    const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
    std::shared_ptr<NiceMock<Network::MockFilterChainManager>> inline_filter_chain_manager_;
  };

  using TestListenerPtr = std::unique_ptr<TestListener>;

  TestListener* addListener(
      uint64_t tag, bool bind_to_port, bool hand_off_restored_destination_connections,
      const std::string& name, Network::Listener* listener,
      Network::ListenerCallbacks** listener_callbacks = nullptr,
      Network::MockConnectionBalancer* connection_balancer = nullptr,
      Network::BalancedConnectionHandler** balanced_connection_handler = nullptr,
      Network::Socket::Type socket_type = Network::Socket::Type::Stream,
      std::chrono::milliseconds listener_filters_timeout = std::chrono::milliseconds(15000),
      bool continue_on_listener_filters_timeout = false,
      std::shared_ptr<NiceMock<Network::MockFilterChainManager>> overridden_filter_chain_manager =
          nullptr) {
    listeners_.emplace_back(std::make_unique<TestListener>(
        *this, tag, bind_to_port, hand_off_restored_destination_connections, name, socket_type,
        listener_filters_timeout, continue_on_listener_filters_timeout, socket_factory_,
        overridden_filter_chain_manager));
    EXPECT_CALL(*socket_factory_, socketType()).WillOnce(Return(socket_type));
    if (listener == nullptr) {
      // Expecting listener config in place update.
      // If so, dispatcher would not create new network listener.
      return listeners_.back().get();
    }
    EXPECT_CALL(*socket_factory_, getListenSocket()).WillOnce(Return(listeners_.back()->socket_));
    if (socket_type == Network::Socket::Type::Stream) {
      EXPECT_CALL(dispatcher_, createListener_(_, _, _))
          .WillOnce(Invoke([listener, listener_callbacks](Network::SocketSharedPtr&&,
                                                          Network::ListenerCallbacks& cb,
                                                          bool) -> Network::Listener* {
            if (listener_callbacks != nullptr) {
              *listener_callbacks = &cb;
            }
            return listener;
          }));
    } else {
      EXPECT_CALL(dispatcher_, createUdpListener_(_, _))
          .WillOnce(Invoke([listener](Network::SocketSharedPtr&&,
                                      Network::UdpListenerCallbacks&) -> Network::UdpListener* {
            return dynamic_cast<Network::UdpListener*>(listener);
          }));
    }

    if (connection_balancer != nullptr) {
      listeners_.back()->connection_balancer_.reset(connection_balancer);
      ASSERT(balanced_connection_handler != nullptr);
      EXPECT_CALL(*connection_balancer, registerHandler(_))
          .WillOnce(SaveArgAddress(balanced_connection_handler));
    }
    return listeners_.back().get();
  }

  Stats::TestUtil::TestStore stats_store_;
  std::shared_ptr<Network::MockListenSocketFactory> socket_factory_;
  Network::Address::InstanceConstSharedPtr local_address_{
      new Network::Address::Ipv4Instance("127.0.0.1", 10001)};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  std::list<TestListenerPtr> listeners_;
  Network::ConnectionHandlerPtr handler_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> factory_;
  const Network::FilterChainSharedPtr filter_chain_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
};

// Verify that if a listener is removed while a rebalanced connection is in flight, we correctly
// destroy the connection.
TEST_F(ConnectionHandlerTest, RemoveListenerDuringRebalance) {
  InSequence s;

  // For reasons I did not investigate, the death test below requires this, likely due to forking.
  // So we just leak the FDs for this test.
  ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  Network::MockConnectionBalancer* connection_balancer = new Network::MockConnectionBalancer();
  Network::BalancedConnectionHandler* current_handler;
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks,
                  connection_balancer, &current_handler);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

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
  ASSERT(post_cb != nullptr);
  EXPECT_CALL(*listener, onDestroy());
#else
  // On release builds this should be fine.
  EXPECT_CALL(*listener, onDestroy());
  handler_->removeListeners(1);
  post_cb();
#endif
}

TEST_F(ConnectionHandlerTest, ListenerConnectionLimitEnforced) {
  Network::ListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, false, false, "test_listener1", listener1, &listener_callbacks1);
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  // Only allow a single connection on this listener.
  test_listener1->setMaxConnections(1);
  handler_->addListener(absl::nullopt, *test_listener1);

  auto listener2 = new NiceMock<Network::MockListener>();
  Network::ListenerCallbacks* listener_callbacks2;
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, &listener_callbacks2);
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  // Do not allow any connections on this listener.
  test_listener2->setMaxConnections(0);
  handler_->addListener(absl::nullopt, *test_listener2);

  EXPECT_CALL(manager_, findFilterChain(_)).WillRepeatedly(Return(filter_chain_.get()));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillRepeatedly(Return(true));
  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(*test_filter, destroy_());
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::Continue;
      }));

  // For listener 2, verify its connection limit is independent of listener 1.

  // We expect that listener 2 accepts the connection, so there will be a call to
  // createServerConnection and active cx should increase, while cx overflow remains the same.
  listener_callbacks2->onAccept(
      Network::ConnectionSocketPtr{new NiceMock<Network::MockConnectionSocket>()});
  EXPECT_EQ(0, handler_->numConnections());
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(0, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_overflow")->value());

  // For listener 1, verify connections are limited after one goes active.

  // First connection attempt should result in an active connection being created.
  auto conn1 = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(conn1));
  listener_callbacks1->onAccept(
      Network::ConnectionSocketPtr{new NiceMock<Network::MockConnectionSocket>()});
  EXPECT_EQ(1, handler_->numConnections());
  // Note that these stats are not the per-worker stats, but the per-listener stats.
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_overflow")->value());

  // Don't expect server connection to be created, should be instantly closed and increment
  // overflow stat.
  listener_callbacks1->onAccept(
      Network::ConnectionSocketPtr{new NiceMock<Network::MockConnectionSocket>()});
  EXPECT_EQ(1, handler_->numConnections());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(2, TestUtility::findCounter(stats_store_, "downstream_cx_overflow")->value());

  // Check behavior again for good measure.
  listener_callbacks1->onAccept(
      Network::ConnectionSocketPtr{new NiceMock<Network::MockConnectionSocket>()});
  EXPECT_EQ(1, handler_->numConnections());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(3, TestUtility::findCounter(stats_store_, "downstream_cx_overflow")->value());

  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
}

TEST_F(ConnectionHandlerTest, RemoveListener) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

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

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
}

TEST_F(ConnectionHandlerTest, AddDisabledListener) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
  handler_->addListener(absl::nullopt, *test_listener);
}

TEST_F(ConnectionHandlerTest, DestroyCloseConnections) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*listener, onDestroy());
  handler_.reset();
}

TEST_F(ConnectionHandlerTest, CloseDuringFilterChainCreate) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

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

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

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
  Network::ListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  handler_->addListener(absl::nullopt, *test_listener1);

  Network::ListenerCallbacks* listener_callbacks2;
  auto listener2 = new NiceMock<Network::MockListener>();
  TestListener* test_listener2 =
      addListener(1, false, false, "test_listener2", listener2, &listener_callbacks2);
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(alt_address));
  handler_->addListener(absl::nullopt, *test_listener2);

  auto* test_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(*test_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(nullptr, Network::ListenerFilterPtr{test_filter});
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
  Network::ListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(normal_address));
  handler_->addListener(absl::nullopt, *test_listener1);

  Network::ListenerCallbacks* listener_callbacks2;
  auto listener2 = new NiceMock<Network::MockListener>();
  TestListener* test_listener2 =
      addListener(1, false, false, "test_listener2", listener2, &listener_callbacks2);
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getIpv4AnyAddress();
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(absl::nullopt, *test_listener2);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(*test_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  bool redirected = false;
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        if (!redirected) {
          manager.addAcceptFilter(listener_filter_matcher_,
                                  Network::ListenerFilterPtr{test_filter});
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
  Network::ListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  // Original dst address nor port number match that of the listener's address.
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(absl::nullopt, *test_listener1);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_filter, destroy_());
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
  Network::ListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);

  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(any_address));
  handler_->addListener(absl::nullopt, *test_listener1);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(*test_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
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
  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*accepted_socket, detectedTransportProtocol())
      .WillOnce(Return(absl::string_view("")));
  EXPECT_CALL(*accepted_socket, setDetectedTransportProtocol(absl::string_view("raw_buffer")));
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TransportProtocolCustom) {
  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(*test_filter, destroy_());
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
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

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
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
  EXPECT_CALL(*test_filter, destroy_());
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

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, nullptr, nullptr,
                  Network::Socket::Type::Stream, std::chrono::milliseconds(15000), true);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* test_filter = new NiceMock<Network::MockListenerFilter>();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
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
  EXPECT_CALL(*test_filter, destroy_());
  // Barrier: test_filter must be destructed before findFilterChain
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

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
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
  EXPECT_CALL(*test_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  EXPECT_CALL(*timeout, disableTimer());
  listener_filter_cb->continueFilterChain(true);

  EXPECT_CALL(*listener, onDestroy());
}

// Ensure there is no timeout when the timeout is disabled with 0s.
TEST_F(ConnectionHandlerTest, ListenerFilterDisabledTimeout) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, nullptr, nullptr,
                  Network::Socket::Type::Stream, std::chrono::milliseconds());
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*test_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

// Listener Filter could close socket in the context of listener callback.
TEST_F(ConnectionHandlerTest, ListenerFilterReportError) {
  InSequence s;

  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockListenerFilter* first_filter = new Network::MockListenerFilter();
  Network::MockListenerFilter* last_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{first_filter});
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{last_filter});
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
  EXPECT_CALL(*first_filter, destroy_());
  EXPECT_CALL(*last_filter, destroy_());
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

  auto listener = new NiceMock<Network::MockUdpListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, nullptr, nullptr, nullptr,
                  Network::Socket::Type::Datagram, std::chrono::milliseconds());
  EXPECT_CALL(factory_, createUdpListenerFilterChain(_, _))
      .WillOnce(Invoke([&](Network::UdpListenerFilterManager&,
                           Network::UdpReadFilterCallbacks&) -> bool { return true; }));
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  EXPECT_CALL(*listener, onDestroy());

  try {
    handler_->addListener(absl::nullopt, *test_listener);
    FAIL();
  } catch (const Network::CreateListenerException& e) {
    EXPECT_THAT(
        e.what(),
        HasSubstr("Cannot create listener as no read filter registered for the udp listener"));
  }
}

TEST_F(ConnectionHandlerTest, TcpListenerInplaceUpdate) {
  InSequence s;
  uint64_t old_listener_tag = 1;
  uint64_t new_listener_tag = 2;
  Network::ListenerCallbacks* old_listener_callbacks;
  auto old_listener = new NiceMock<Network::MockListener>();
  TestListener* old_test_listener = addListener(old_listener_tag, true, false, "test_listener",
                                                old_listener, &old_listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *old_test_listener);
  ASSERT_NE(old_test_listener, nullptr);

  Network::ListenerCallbacks* new_listener_callbacks = nullptr;

  auto overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  TestListener* new_test_listener =
      addListener(new_listener_tag, true, false, "test_listener", /* Network::Listener */ nullptr,
                  &new_listener_callbacks, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, overridden_filter_chain_manager);
  handler_->addListener(old_listener_tag, *new_test_listener);
  ASSERT_EQ(new_listener_callbacks, nullptr)
      << "new listener should be inplace added and callback should not change";

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(manager_, findFilterChain(_)).Times(0);
  EXPECT_CALL(*overridden_filter_chain_manager, findFilterChain(_)).WillOnce(Return(nullptr));
  old_listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_CALL(*old_listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveFilterChain) {
  InSequence s;
  uint64_t listener_tag = 1;
  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(listener_tag, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillOnce(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(filter_chain_.get()));
  Network::MockConnection* server_connection = new NiceMock<Network::MockConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(server_connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));

  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});

  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  const std::list<const Network::FilterChain*> filter_chains{filter_chain_.get()};

  // The completion callback is scheduled
  handler_->removeFilterChains(listener_tag, filter_chains,
                               []() { ENVOY_LOG(debug, "removed filter chains"); });
  // Trigger the deletion if any.
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*listener, onDestroy());
  handler_.reset();
}

// Listener Filter matchers works.
TEST_F(ConnectionHandlerTest, ListenerFilterWorks) {
  Network::ListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*socket_factory_, localAddress()).WillRepeatedly(ReturnRef(local_address_));
  handler_->addListener(absl::nullopt, *test_listener);

  auto all_matcher = std::make_shared<Network::MockListenerFilterMatcher>();
  auto* disabled_listener_filter = new Network::MockListenerFilter();
  auto* enabled_filter = new Network::MockListenerFilter();
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(all_matcher, Network::ListenerFilterPtr{disabled_listener_filter});
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{enabled_filter});
        return true;
      }));

  // The all matcher matches any incoming traffic and disables the listener filter.
  EXPECT_CALL(*all_matcher, matches(_)).WillOnce(Return(true));
  EXPECT_CALL(*disabled_listener_filter, onAccept(_)).Times(0);

  // The non matcher acts as if always enabled.
  EXPECT_CALL(*enabled_filter, onAccept(_)).WillOnce(Return(Network::FilterStatus::Continue));
  EXPECT_CALL(*disabled_listener_filter, destroy_());
  EXPECT_CALL(*enabled_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_)).WillOnce(Return(nullptr));
  listener_callbacks->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());
  EXPECT_CALL(*listener, onDestroy());
}

} // namespace
} // namespace Server
} // namespace Envoy
