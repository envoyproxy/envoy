#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/extensions/bootstrap/internal_listener/active_internal_listener.h"
#include "source/extensions/bootstrap/internal_listener/thread_local_registry.h"
#include "source/server/connection_handler_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {
namespace {

class ActiveInternalListenerTest : public testing::Test,
                                   protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveInternalListenerTest() {
    EXPECT_CALL(listener_config_, listenerScope).Times(testing::AnyNumber());
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));
    listener_filter_matcher_ = std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>();
  }
  void addListener() {
    EXPECT_CALL(listener_config_, listenerFiltersTimeout());
    EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
    EXPECT_CALL(listener_config_, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
    EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));
    auto mock_listener_will_be_moved = std::make_unique<Network::MockListener>();
    generic_listener_ = mock_listener_will_be_moved.get();
    internal_listener_ = std::make_shared<ActiveInternalListener>(
        conn_handler_, dispatcher_, std::move(mock_listener_will_be_moved), listener_config_);
  }
  Network::Listener* addListenerWithRealNetworkListener() {
    EXPECT_CALL(listener_config_, listenerFiltersTimeout());
    EXPECT_CALL(listener_config_, continueOnListenerFiltersTimeout());
    EXPECT_CALL(listener_config_, filterChainManager()).WillRepeatedly(ReturnRef(manager_));
    EXPECT_CALL(listener_config_, openConnections()).WillRepeatedly(ReturnRef(resource_limit_));

    internal_listener_ =
        std::make_shared<ActiveInternalListener>(conn_handler_, dispatcher_, listener_config_);
    return internal_listener_->listener();
  }
  void expectFilterChainFactory() {
    EXPECT_CALL(listener_config_, filterChainFactory())
        .WillRepeatedly(ReturnRef(filter_chain_factory_));
  }
  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  BasicResourceLimitImpl resource_limit_;
  Network::MockConnectionHandler conn_handler_;
  Network::MockListener* generic_listener_;
  Network::MockListenerConfig listener_config_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> filter_chain_factory_;
  std::shared_ptr<Network::MockFilterChain> filter_chain_;
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
  std::shared_ptr<ActiveInternalListener> internal_listener_;
};

TEST_F(ActiveInternalListenerTest, BasicInternalListener) {
  addListener();
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, AcceptSocketAndCreateListenerFilter) {
  addListener();
  expectFilterChainFactory();
  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter();
  // FIX-ME: replace by mock socket
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_listener_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, DestroyListenerClosesActiveSocket) {
  addListener();
  expectFilterChainFactory();
  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter(10);
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(*accepted_socket, ioHandle()).WillRepeatedly(ReturnRef(io_handle));
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));

  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*test_listener_filter, destroy_());
  EXPECT_CALL(*generic_listener_, onDestroy());
  internal_listener_.reset();
}

TEST_F(ActiveInternalListenerTest, AcceptSocketAndCreateNetworkFilter) {
  addListener();
  expectFilterChainFactory();

  Network::MockListenerFilter* test_listener_filter = new Network::MockListenerFilter();
  // FIX-ME: replace by mock socket
  Network::Address::InstanceConstSharedPtr original_dst_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 8080));

  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        // Insert the Mock filter.
        manager.addAcceptFilter(listener_filter_matcher_,
                                Network::ListenerFilterPtr{test_listener_filter});
        return true;
      }));
  EXPECT_CALL(*test_listener_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(original_dst_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(*test_listener_filter, destroy_());
  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();
  auto transport_socket_factory = Network::Test::createRawBufferDownstreamSocketFactory();

  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(*filter_chain_, transportSocketFactory)
      .WillOnce(testing::ReturnRef(*transport_socket_factory));
  EXPECT_CALL(*filter_chain_, networkFilterFactories).WillOnce(ReturnRef(*filter_factory_callback));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(filter_chain_factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(listener_config_, perConnectionBufferLimitBytes());
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_CALL(conn_handler_, decNumConnections());
  connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(*generic_listener_, onDestroy());
}

TEST_F(ActiveInternalListenerTest, StopListener) {
  addListener();
  EXPECT_CALL(*generic_listener_, onDestroy());
  internal_listener_->shutdownListener();
}

TEST_F(ActiveInternalListenerTest, PausedListenerAcceptNewSocket) {
  addListenerWithRealNetworkListener();
  internal_listener_->pauseListening();

  expectFilterChainFactory();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager&) -> bool { return true; }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});
}

TEST_F(ActiveInternalListenerTest, DestroyListenerCloseAllConnections) {
  addListenerWithRealNetworkListener();
  internal_listener_->pauseListening();

  expectFilterChainFactory();
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();

  auto filter_factory_callback = std::make_shared<std::vector<Network::FilterFactoryCb>>();
  filter_chain_ = std::make_shared<NiceMock<Network::MockFilterChain>>();
  auto transport_socket_factory = Network::Test::createRawBufferDownstreamSocketFactory();

  EXPECT_CALL(filter_chain_factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager&) -> bool { return true; }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(*filter_chain_, transportSocketFactory)
      .WillOnce(testing::ReturnRef(*transport_socket_factory));
  EXPECT_CALL(*filter_chain_, networkFilterFactories).WillOnce(ReturnRef(*filter_factory_callback));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(conn_handler_, incNumConnections());
  EXPECT_CALL(filter_chain_factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(listener_config_, perConnectionBufferLimitBytes());
  internal_listener_->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(conn_handler_, decNumConnections());
  internal_listener_.reset();
}

class ConnectionHandlerTest : public testing::Test, protected Logger::Loggable<Logger::Id::main> {
public:
  ConnectionHandlerTest()
      : handler_(new Server::ConnectionHandlerImpl(dispatcher_, 0)),
        filter_chain_(std::make_shared<NiceMock<Network::MockFilterChain>>()),
        listener_filter_matcher_(std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>()),
        access_log_(std::make_shared<AccessLog::MockInstance>()) {
    ON_CALL(*filter_chain_, transportSocketFactory)
        .WillByDefault(ReturnPointee(std::shared_ptr<Network::DownstreamTransportSocketFactory>{
            Network::Test::createRawBufferDownstreamSocketFactory()}));
    ON_CALL(*filter_chain_, networkFilterFactories)
        .WillByDefault(ReturnPointee(std::make_shared<std::vector<Network::FilterFactoryCb>>()));
    ON_CALL(*listener_filter_matcher_, matches(_)).WillByDefault(Return(false));
  }

  class TestListener : public Network::ListenerConfig {
  public:
    TestListener(
        ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
        bool hand_off_restored_destination_connections, const std::string& name,
        Network::Socket::Type socket_type, std::chrono::milliseconds listener_filters_timeout,
        bool continue_on_listener_filters_timeout,
        std::shared_ptr<AccessLog::MockInstance> access_log,
        std::shared_ptr<NiceMock<Network::MockFilterChainManager>> filter_chain_manager = nullptr,
        uint32_t tcp_backlog_size = ENVOY_TCP_BACKLOG_SIZE,
        Network::ConnectionBalancerSharedPtr connection_balancer = nullptr,
        bool ignore_global_conn_limit = false)
        : parent_(parent), socket_(std::make_shared<NiceMock<Network::MockListenSocket>>()),
          tag_(tag), bind_to_port_(bind_to_port), tcp_backlog_size_(tcp_backlog_size),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          name_(name), listener_filters_timeout_(listener_filters_timeout),
          continue_on_listener_filters_timeout_(continue_on_listener_filters_timeout),
          connection_balancer_(connection_balancer == nullptr
                                   ? std::make_shared<Network::NopConnectionBalancerImpl>()
                                   : connection_balancer),
          access_logs_({access_log}), inline_filter_chain_manager_(filter_chain_manager),
          init_manager_(nullptr), ignore_global_conn_limit_(ignore_global_conn_limit) {
      socket_factories_.emplace_back(std::make_unique<Network::MockListenSocketFactory>());
      ON_CALL(*socket_, socketType()).WillByDefault(Return(socket_type));
    }

    class MockInternalListenerRegistry : public Network::InternalListenerRegistry {
    public:
      Network::LocalInternalListenerRegistry* getLocalRegistry() override {
        return &local_registry_;
      }
      ThreadLocalRegistryImpl local_registry_;
    };

    class InternalListenerConfigImpl : public Network::InternalListenerConfig {
    public:
      InternalListenerConfigImpl(Network::InternalListenerRegistry& registry)
          : registry_(registry) {}
      Network::InternalListenerRegistry& internalListenerRegistry() override { return registry_; }
      Network::InternalListenerRegistry& registry_;
    };

    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override {
      return inline_filter_chain_manager_ == nullptr ? parent_.manager_
                                                     : *inline_filter_chain_manager_;
    }
    Network::FilterChainFactory& filterChainFactory() override { return parent_.factory_; }
    std::vector<Network::ListenSocketFactoryPtr>& listenSocketFactories() override {
      return socket_factories_;
    }
    bool bindToPort() const override { return bind_to_port_; }
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
    Network::UdpListenerConfigOptRef udpListenerConfig() override { return {}; }
    Network::InternalListenerConfigOptRef internalListenerConfig() override {
      if (internal_listener_config_ == nullptr) {
        return {};
      } else {
        return *internal_listener_config_;
      }
    }
    envoy::config::core::v3::TrafficDirection direction() const override { return direction_; }
    void setDirection(envoy::config::core::v3::TrafficDirection direction) {
      direction_ = direction;
    }
    Network::ConnectionBalancer& connectionBalancer(const Network::Address::Instance&) override {
      return *connection_balancer_;
    }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return access_logs_;
    }
    ResourceLimit& openConnections() override { return open_connections_; }
    uint32_t tcpBacklogSize() const override { return tcp_backlog_size_; }
    Init::Manager& initManager() override { return *init_manager_; }
    bool ignoreGlobalConnLimit() const override { return ignore_global_conn_limit_; }
    void setMaxConnections(const uint32_t num_connections) {
      open_connections_.setMax(num_connections);
    }
    void clearMaxConnections() { open_connections_.resetMax(); }

    ConnectionHandlerTest& parent_;
    std::shared_ptr<NiceMock<Network::MockListenSocket>> socket_;
    std::vector<Network::ListenSocketFactoryPtr> socket_factories_;
    uint64_t tag_;
    bool bind_to_port_;
    const uint32_t tcp_backlog_size_;
    const bool hand_off_restored_destination_connections_;
    const std::string name_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const bool continue_on_listener_filters_timeout_;
    std::unique_ptr<InternalListenerConfigImpl> internal_listener_config_;
    Network::ConnectionBalancerSharedPtr connection_balancer_;
    BasicResourceLimitImpl open_connections_;
    const std::vector<AccessLog::InstanceSharedPtr> access_logs_;
    std::shared_ptr<NiceMock<Network::MockFilterChainManager>> inline_filter_chain_manager_;
    std::unique_ptr<Init::Manager> init_manager_;
    const bool ignore_global_conn_limit_;
    envoy::config::core::v3::TrafficDirection direction_;
  };

  using TestListenerPtr = std::unique_ptr<TestListener>;

  TestListener* addInternalListener(
      uint64_t tag, const std::string& name,
      std::chrono::milliseconds listener_filters_timeout = std::chrono::milliseconds(15000),
      bool continue_on_listener_filters_timeout = false,
      std::shared_ptr<NiceMock<Network::MockFilterChainManager>> overridden_filter_chain_manager =
          nullptr) {
    listeners_.emplace_back(std::make_unique<TestListener>(
        *this, tag, /*bind_to_port*/ false, /*hand_off_restored_destination_connections*/ false,
        name, Network::Socket::Type::Stream, listener_filters_timeout,
        continue_on_listener_filters_timeout, access_log_, overridden_filter_chain_manager,
        ENVOY_TCP_BACKLOG_SIZE, nullptr));
    listeners_.back()->internal_listener_config_ =
        std::make_unique<TestListener::InternalListenerConfigImpl>(internal_listener_registry_);
    return listeners_.back().get();
  }

  Stats::TestUtil::TestStore stats_store_;
  Network::Address::InstanceConstSharedPtr local_address_{
      new Network::Address::Ipv4Instance("127.0.0.1", 10001)};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  TestListener::MockInternalListenerRegistry internal_listener_registry_;
  std::list<TestListenerPtr> listeners_;
  std::unique_ptr<Server::ConnectionHandlerImpl> handler_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> factory_;
  const std::shared_ptr<Network::MockFilterChain> filter_chain_;
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
  std::shared_ptr<AccessLog::MockInstance> access_log_;
  TestScopedRuntime scoped_runtime_;
  Runtime::Loader& runtime_{scoped_runtime_.loader()};
};

TEST_F(ConnectionHandlerTest, DisableInternalListener) {
  InSequence s;
  Network::Address::InstanceConstSharedPtr local_address{
      new Network::Address::EnvoyInternalInstance("server_internal_address")};

  TestListener* internal_listener =
      addInternalListener(1, "test_internal_listener", std::chrono::milliseconds(), false, nullptr);
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  internal_listener->socket_factories_[0].get()),
              localAddress())
      .WillRepeatedly(ReturnRef(local_address));
  handler_->addListener(absl::nullopt, *internal_listener, runtime_);
  auto internal_listener_cb = handler_->findByAddress(local_address);
  ASSERT_TRUE(internal_listener_cb.has_value());

  handler_->disableListeners();
  auto internal_listener_cb_disabled = handler_->findByAddress(local_address);
  ASSERT_TRUE(internal_listener_cb_disabled.has_value());
  ASSERT_EQ(&internal_listener_cb_disabled.value().get(), &internal_listener_cb.value().get());

  handler_->enableListeners();
  auto internal_listener_cb_enabled = handler_->findByAddress(local_address);
  ASSERT_TRUE(internal_listener_cb_enabled.has_value());
  ASSERT_EQ(&internal_listener_cb_enabled.value().get(), &internal_listener_cb.value().get());
}

TEST_F(ConnectionHandlerTest, InternalListenerInplaceUpdate) {
  InSequence s;
  uint64_t old_listener_tag = 1;
  uint64_t new_listener_tag = 2;
  Network::Address::InstanceConstSharedPtr local_address{
      new Network::Address::EnvoyInternalInstance("server_internal_address")};

  TestListener* internal_listener = addInternalListener(
      old_listener_tag, "test_internal_listener", std::chrono::milliseconds(), false, nullptr);
  EXPECT_CALL(*static_cast<Network::MockListenSocketFactory*>(
                  internal_listener->socket_factories_[0].get()),
              localAddress())
      .WillRepeatedly(ReturnRef(local_address));
  handler_->addListener(absl::nullopt, *internal_listener, runtime_);

  ASSERT_NE(internal_listener, nullptr);

  auto overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  TestListener* new_test_listener =
      addInternalListener(new_listener_tag, "test_internal_listener", std::chrono::milliseconds(),
                          false, overridden_filter_chain_manager);

  handler_->addListener(old_listener_tag, *new_test_listener, runtime_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();

  auto internal_listener_cb = handler_->findByAddress(local_address);

  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  EXPECT_CALL(*overridden_filter_chain_manager, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _, _, _));
  internal_listener_cb.value().get().onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  testing::MockFunction<void()> completion;
  handler_->removeFilterChains(old_listener_tag, {}, completion.AsStdFunction());
  EXPECT_CALL(completion, Call());
  dispatcher_.clearDeferredDeleteList();
}

} // namespace
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
