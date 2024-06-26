#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/io_error.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/udp_listener_config.pb.h"
#include "envoy/network/exception.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/listener_manager/active_raw_udp_listener_config.h"
#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::Invoke;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace {

class MockConnectionHandlerImpl : public ConnectionHandlerImpl {
public:
  using ConnectionHandlerImpl::ConnectionHandlerImpl;
  MOCK_METHOD(Network::ListenerPtr, createListener,
              (Network::SocketSharedPtr && socket, Network::TcpListenerCallbacks& cb,
               Runtime::Loader& runtime, Random::RandomGenerator& random,
               const Network::ListenerConfig& listener_config,
               Server::ThreadLocalOverloadStateOptRef overload_state));
};

class ConnectionHandlerTest : public testing::Test,
                              protected Logger::Loggable<Logger::Id::main>,
                              public Event::TestUsingSimulatedTime {
public:
  ConnectionHandlerTest()
      : handler_(new MockConnectionHandlerImpl(dispatcher_, 0)),
        filter_chain_(std::make_shared<NiceMock<Network::MockFilterChain>>()),
        listener_filter_matcher_(std::make_shared<NiceMock<Network::MockListenerFilterMatcher>>()),
        access_log_(std::make_shared<AccessLog::MockInstance>()) {
    ON_CALL(*filter_chain_, transportSocketFactory)
        .WillByDefault(ReturnPointee(std::shared_ptr<Network::DownstreamTransportSocketFactory>{
            Network::Test::createRawBufferDownstreamSocketFactory()}));
    ON_CALL(*filter_chain_, networkFilterFactories)
        .WillByDefault(ReturnPointee(std::make_shared<Filter::NetworkFilterFactoriesList>()));
    ON_CALL(*listener_filter_matcher_, matches(_)).WillByDefault(Return(false));
  }

  class TestListenerBase : public Network::ListenerConfig {
  public:
    TestListenerBase(
        ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
        bool hand_off_restored_destination_connections, const std::string& name,
        Network::Socket::Type socket_type, std::chrono::milliseconds listener_filters_timeout,
        bool continue_on_listener_filters_timeout,
        std::shared_ptr<AccessLog::MockInstance> access_log,
        std::shared_ptr<NiceMock<Network::MockFilterChainManager>> filter_chain_manager = nullptr,
        uint32_t tcp_backlog_size = ENVOY_TCP_BACKLOG_SIZE, bool ignore_global_conn_limit = false,
        int num_of_socket_factories = 1)
        : parent_(parent), tag_(tag), bind_to_port_(bind_to_port),
          tcp_backlog_size_(tcp_backlog_size),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          name_(name), listener_filters_timeout_(listener_filters_timeout),
          continue_on_listener_filters_timeout_(continue_on_listener_filters_timeout),
          access_logs_({access_log}), inline_filter_chain_manager_(filter_chain_manager),
          init_manager_(nullptr), ignore_global_conn_limit_(ignore_global_conn_limit),
          listener_info_(std::make_shared<NiceMock<Network::MockListenerInfo>>()) {
      for (int i = 0; i < num_of_socket_factories; i++) {
        socket_factories_.emplace_back(std::make_unique<Network::MockListenSocketFactory>());
        sockets_.emplace_back(std::make_shared<NiceMock<Network::MockListenSocket>>());
        ON_CALL(*sockets_.back().get(), socketType()).WillByDefault(Return(socket_type));
      }
      envoy::config::listener::v3::UdpListenerConfig udp_config;
      udp_listener_config_ = std::make_unique<UdpListenerConfigImpl>(udp_config);
      udp_listener_config_->listener_factory_ =
          std::make_unique<Server::ActiveRawUdpListenerFactory>(1);
      udp_listener_config_->writer_factory_ = std::make_unique<Network::UdpDefaultWriterFactory>();
      ON_CALL(*static_cast<Network::MockListenerInfo*>(
                  const_cast<Network::ListenerInfo*>(listener_info_.get())),
              direction())
          .WillByDefault(Return(direction_));
    }

    struct UdpListenerConfigImpl : public Network::UdpListenerConfig {
      UdpListenerConfigImpl(const envoy::config::listener::v3::UdpListenerConfig& config)
          : config_(config) {}

      // Network::UdpListenerConfig
      Network::ActiveUdpListenerFactory& listenerFactory() override { return *listener_factory_; }
      Network::UdpPacketWriterFactory& packetWriterFactory() override { return *writer_factory_; }
      Network::UdpListenerWorkerRouter&
      listenerWorkerRouter(const Network::Address::Instance& address) override {
        auto iter = listener_worker_router_map_.find(address.asString());
        EXPECT_NE(iter, listener_worker_router_map_.end());
        return *iter->second;
      }
      const envoy::config::listener::v3::UdpListenerConfig& config() override { return config_; }

      const envoy::config::listener::v3::UdpListenerConfig config_;
      std::unique_ptr<Network::ActiveUdpListenerFactory> listener_factory_;
      std::unique_ptr<Network::UdpPacketWriterFactory> writer_factory_;
      absl::flat_hash_map<std::string, Network::UdpListenerWorkerRouterPtr>
          listener_worker_router_map_;
    };

    // A helper to get the reference of listen socket factory.
    Network::MockListenSocketFactory& socketFactory(int index = 0) {
      return *static_cast<Network::MockListenSocketFactory*>(socket_factories_[index].get());
    }

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
    Stats::Scope& listenerScope() override { return *parent_.stats_store_.rootScope(); }
    uint64_t listenerTag() const override { return tag_; }
    const std::string& name() const override { return name_; }
    Network::UdpListenerConfigOptRef udpListenerConfig() override { return *udp_listener_config_; }
    Network::InternalListenerConfigOptRef internalListenerConfig() override { return {}; }
    const Network::ListenerInfoConstSharedPtr& listenerInfo() const override {
      return listener_info_;
    }
    void setDirection(envoy::config::core::v3::TrafficDirection direction) {
      direction_ = direction;
    }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return access_logs_;
    }
    ResourceLimit& openConnections() override { return open_connections_; }
    uint32_t tcpBacklogSize() const override { return tcp_backlog_size_; }
    uint32_t maxConnectionsToAcceptPerSocketEvent() const override {
      return Network::DefaultMaxConnectionsToAcceptPerSocketEvent;
    }
    Init::Manager& initManager() override { return *init_manager_; }
    bool ignoreGlobalConnLimit() const override { return ignore_global_conn_limit_; }
    bool shouldBypassOverloadManager() const override { return false; }
    void setMaxConnections(const uint32_t num_connections) {
      open_connections_.setMax(num_connections);
    }
    void clearMaxConnections() { open_connections_.resetMax(); }

    ConnectionHandlerTest& parent_;
    std::vector<std::shared_ptr<NiceMock<Network::MockListenSocket>>> sockets_;
    std::vector<Network::ListenSocketFactoryPtr> socket_factories_;
    uint64_t tag_;
    bool bind_to_port_;
    const uint32_t tcp_backlog_size_;
    const bool hand_off_restored_destination_connections_;
    const std::string name_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const bool continue_on_listener_filters_timeout_;
    std::unique_ptr<UdpListenerConfigImpl> udp_listener_config_;
    BasicResourceLimitImpl open_connections_;
    const std::vector<AccessLog::InstanceSharedPtr> access_logs_;
    std::shared_ptr<NiceMock<Network::MockFilterChainManager>> inline_filter_chain_manager_;
    std::unique_ptr<Init::Manager> init_manager_;
    const bool ignore_global_conn_limit_;
    envoy::config::core::v3::TrafficDirection direction_;
    absl::flat_hash_map<std::string, Network::UdpListenerCallbacks*> udp_listener_callback_map_{};
    Network::ListenerInfoConstSharedPtr listener_info_;
  };

  class TestListener : public TestListenerBase {
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
        bool ignore_global_conn_limit = false, int num_of_socket_factories = 1)
        : TestListenerBase(parent, tag, bind_to_port, hand_off_restored_destination_connections,
                           name, socket_type, listener_filters_timeout,
                           continue_on_listener_filters_timeout, access_log, filter_chain_manager,
                           tcp_backlog_size, ignore_global_conn_limit, num_of_socket_factories),
          connection_balancer_(connection_balancer == nullptr
                                   ? std::make_shared<Network::NopConnectionBalancerImpl>()
                                   : connection_balancer) {}
    Network::ConnectionBalancer& connectionBalancer(const Network::Address::Instance&) override {
      return *connection_balancer_;
    }

    Network::ConnectionBalancerSharedPtr connection_balancer_;
  };

  class TestMultiAddressesListener : public TestListenerBase {
  public:
    TestMultiAddressesListener(
        ConnectionHandlerTest& parent, uint64_t tag, bool bind_to_port,
        bool hand_off_restored_destination_connections, const std::string& name,
        Network::Socket::Type socket_type, std::chrono::milliseconds listener_filters_timeout,
        bool continue_on_listener_filters_timeout,
        std::shared_ptr<AccessLog::MockInstance> access_log,
        absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr>&
            connection_balancers,
        std::shared_ptr<NiceMock<Network::MockFilterChainManager>> filter_chain_manager = nullptr,
        uint32_t tcp_backlog_size = ENVOY_TCP_BACKLOG_SIZE, bool ignore_global_conn_limit = false,
        int num_of_socket_factories = 1)
        : TestListenerBase(parent, tag, bind_to_port, hand_off_restored_destination_connections,
                           name, socket_type, listener_filters_timeout,
                           continue_on_listener_filters_timeout, access_log, filter_chain_manager,
                           tcp_backlog_size, ignore_global_conn_limit, num_of_socket_factories),
          connection_balancers_(connection_balancers) {}

    Network::ConnectionBalancer&
    connectionBalancer(const Network::Address::Instance& address) override {
      auto iter = connection_balancers_.find(address.asString());
      EXPECT_NE(iter, connection_balancers_.end());
      return *iter->second;
    }

    absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers_;
  };

  using TestListenerPtr = std::unique_ptr<TestListenerBase>;

  class MockUpstreamUdpFilter : public Network::UdpListenerReadFilter {
  public:
    MockUpstreamUdpFilter(ConnectionHandlerTest& parent, Network::UdpReadFilterCallbacks& callbacks)
        : UdpListenerReadFilter(callbacks), parent_(parent) {}
    ~MockUpstreamUdpFilter() override {
      parent_.deleted_before_listener_ = !parent_.udp_listener_deleted_;
    }

    MOCK_METHOD(Network::FilterStatus, onData, (Network::UdpRecvData&), (override));
    MOCK_METHOD(Network::FilterStatus, onReceiveError, (Api::IoError::IoErrorCode), (override));

  private:
    ConnectionHandlerTest& parent_;
  };

  class MockUpstreamUdpListener : public Network::UdpListener {
  public:
    explicit MockUpstreamUdpListener(ConnectionHandlerTest& parent) : parent_(parent) {
      ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    }
    ~MockUpstreamUdpListener() override { parent_.udp_listener_deleted_ = true; }

    MOCK_METHOD(void, enable, (), (override));
    MOCK_METHOD(void, disable, (), (override));
    MOCK_METHOD(void, setRejectFraction, (UnitFloat));
    MOCK_METHOD(void, configureLoadShedPoints, (Server::LoadShedPointProvider&));
    MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
    MOCK_METHOD(Network::Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
    MOCK_METHOD(Api::IoCallUint64Result, send, (const Network::UdpSendData&), (override));
    MOCK_METHOD(Api::IoCallUint64Result, flush, (), (override));
    MOCK_METHOD(void, activateRead, (), (override));
    MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const, override));

  private:
    ConnectionHandlerTest& parent_;
    Event::MockDispatcher dispatcher_;
  };

  TestListener* addListener(
      uint64_t tag, bool bind_to_port, bool hand_off_restored_destination_connections,
      const std::string& name, Network::Listener* listener,
      Network::TcpListenerCallbacks** listener_callbacks = nullptr,
      Network::Address::InstanceConstSharedPtr address = nullptr,
      std::shared_ptr<Network::MockConnectionBalancer> connection_balancer = nullptr,
      Network::BalancedConnectionHandler** balanced_connection_handler = nullptr,
      Network::Socket::Type socket_type = Network::Socket::Type::Stream,
      std::chrono::milliseconds listener_filters_timeout = std::chrono::milliseconds(15000),
      bool continue_on_listener_filters_timeout = false,
      std::shared_ptr<NiceMock<Network::MockFilterChainManager>> overridden_filter_chain_manager =
          nullptr,
      uint32_t tcp_backlog_size = ENVOY_TCP_BACKLOG_SIZE, bool ignore_global_conn_limit = false) {
    auto test_listener = std::make_unique<TestListener>(
        *this, tag, bind_to_port, hand_off_restored_destination_connections, name, socket_type,
        listener_filters_timeout, continue_on_listener_filters_timeout, access_log_,
        overridden_filter_chain_manager, tcp_backlog_size, connection_balancer,
        ignore_global_conn_limit);
    TestListener* test_listener_raw_ptr = test_listener.get();
    listeners_.emplace_back(std::move(test_listener));

    if (listener == nullptr) {
      // Expecting listener config in place update.
      // If so, dispatcher would not create new network listener.
      return test_listener_raw_ptr;
    }
    EXPECT_CALL(listeners_.back()->socketFactory(), socketType()).WillOnce(Return(socket_type));
    if (address == nullptr) {
      EXPECT_CALL(listeners_.back()->socketFactory(), localAddress())
          .WillRepeatedly(ReturnRef(local_address_));
      listeners_.back()->sockets_[0]->connection_info_provider_->setLocalAddress(local_address_);
    } else {
      EXPECT_CALL(listeners_.back()->socketFactory(), localAddress())
          .WillRepeatedly(ReturnRefOfCopy(address));
      listeners_.back()->sockets_[0]->connection_info_provider_->setLocalAddress(address);
    }
    EXPECT_CALL(listeners_.back()->socketFactory(), getListenSocket(_))
        .WillOnce(Return(listeners_.back()->sockets_[0]));
    if (socket_type == Network::Socket::Type::Stream) {
      EXPECT_CALL(*handler_, createListener(_, _, _, _, _, _))
          .WillOnce(
              Invoke([listener, listener_callbacks](
                         Network::SocketSharedPtr&&, Network::TcpListenerCallbacks& cb,
                         Runtime::Loader&, Random::RandomGenerator&, const Network::ListenerConfig&,
                         Server::ThreadLocalOverloadStateOptRef) -> Network::ListenerPtr {
                if (listener_callbacks != nullptr) {
                  *listener_callbacks = &cb;
                }
                return std::unique_ptr<Network::Listener>(listener);
              }));
    } else {
      delete listener;
      if (address == nullptr) {
        listeners_.back()->udp_listener_config_->listener_worker_router_map_.emplace(
            local_address_->asString(),
            std::make_unique<NiceMock<Network::MockUdpListenerWorkerRouter>>());
      } else {
        listeners_.back()->udp_listener_config_->listener_worker_router_map_.emplace(
            address->asString(),
            std::make_unique<NiceMock<Network::MockUdpListenerWorkerRouter>>());
      }
    }

    if (balanced_connection_handler != nullptr) {
      EXPECT_CALL(*connection_balancer, registerHandler(_))
          .WillOnce(SaveArgAddress(balanced_connection_handler));
    }

    return test_listener_raw_ptr;
  }

  TestMultiAddressesListener* addMultiAddrsListener(
      uint64_t tag, bool bind_to_port, bool hand_off_restored_destination_connections,
      const std::string& name, std::vector<Network::Listener*>& mock_listeners,
      std::vector<Network::Address::InstanceConstSharedPtr>& addresses,
      absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr>& connection_balancers,
      absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**>& listener_callbacks_map,
      bool disable_listener = false,
      Network::Socket::Type socket_type = Network::Socket::Type::Stream) {
    if (connection_balancers.empty() && socket_type == Network::Socket::Type::Stream) {
      for (auto& address : addresses) {
        connection_balancers.emplace(address->asString(),
                                     std::make_shared<Network::NopConnectionBalancerImpl>());
      }
    }
    auto test_listener = std::make_unique<TestMultiAddressesListener>(
        *this, tag, bind_to_port, hand_off_restored_destination_connections, name, socket_type,
        std::chrono::milliseconds(15000), false, access_log_, connection_balancers, nullptr,
        ENVOY_TCP_BACKLOG_SIZE, false, mock_listeners.size());
    TestMultiAddressesListener* test_listener_raw_ptr = test_listener.get();
    listeners_.emplace_back(std::move(test_listener));

    EXPECT_CALL(listeners_.back()->socketFactory(0), socketType()).WillOnce(Return(socket_type));
    for (std::vector<Network::Listener*>::size_type i = 0; i < mock_listeners.size(); i++) {
      EXPECT_CALL(listeners_.back()->socketFactory(i), localAddress())
          .WillRepeatedly(ReturnRef(addresses[i]));
      EXPECT_CALL(listeners_.back()->socketFactory(i), getListenSocket(_))
          .WillOnce(Return(listeners_.back()->sockets_[i]));
      test_listener_raw_ptr->sockets_[i]->connection_info_provider_->setLocalAddress(addresses[i]);

      if (socket_type == Network::Socket::Type::Stream) {
        EXPECT_CALL(*handler_, createListener(_, _, _, _, _, _))
            .WillOnce(Invoke([i, &mock_listeners, &listener_callbacks_map](
                                 Network::SocketSharedPtr&& socket,
                                 Network::TcpListenerCallbacks& cb, Runtime::Loader&,
                                 Random::RandomGenerator&, const Network::ListenerConfig&,
                                 Server::ThreadLocalOverloadStateOptRef) -> Network::ListenerPtr {
              auto listener_callbacks_iter = listener_callbacks_map.find(
                  socket->connectionInfoProvider().localAddress()->asString());
              EXPECT_NE(listener_callbacks_iter, listener_callbacks_map.end());
              *listener_callbacks_iter->second = &cb;
              return std::unique_ptr<Network::Listener>(mock_listeners[i]);
            }))
            .RetiresOnSaturation();
      } else {
        listeners_.back()->udp_listener_config_->listener_worker_router_map_.emplace(
            addresses[i]->asString(),
            std::make_unique<NiceMock<Network::MockUdpListenerWorkerRouter>>());
      }

      if (disable_listener) {
        EXPECT_CALL(*static_cast<Network::MockListener*>(mock_listeners[i]), disable());
      }
    }

    return test_listener_raw_ptr;
  }

  void validateOriginalDst(Network::TcpListenerCallbacks** listener_callbacks,
                           TestListener* test_listener, Network::MockListener* listener) {
    Network::Address::InstanceConstSharedPtr normal_address(
        new Network::Address::Ipv4Instance("127.0.0.1", 80));
    // Original dst address nor port number match that of the listener's address.
    Network::Address::InstanceConstSharedPtr original_dst_address(
        new Network::Address::Ipv4Instance("127.0.0.2", 8080));
    Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
        *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
    EXPECT_CALL(test_listener->socketFactory(), localAddress())
        .WillRepeatedly(ReturnRef(any_address));
    handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

    Network::MockListenerFilter* test_filter = new Network::MockListenerFilter();
    Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
    EXPECT_CALL(factory_, createListenerFilterChain(_))
        .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
          // Insert the Mock filter.
          manager.addAcceptFilter(listener_filter_matcher_,
                                  Network::ListenerFilterPtr{test_filter});
          return true;
        }));
    EXPECT_CALL(*test_filter, onAccept(_))
        .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
          cb.socket().connectionInfoProvider().restoreLocalAddress(original_dst_address);
          return Network::FilterStatus::Continue;
        }));
    EXPECT_CALL(*test_filter, destroy_());
    EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
    auto* connection = new NiceMock<Network::MockServerConnection>();
    EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
    EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
    (*listener_callbacks)->onAccept(Network::ConnectionSocketPtr{accepted_socket});
    EXPECT_EQ(1UL, handler_->numConnections());

    EXPECT_CALL(*listener, onDestroy());
    EXPECT_CALL(*access_log_, log(_, _));
  }

  Stats::TestUtil::TestStore stats_store_;
  Network::Address::InstanceConstSharedPtr local_address_{
      new Network::Address::Ipv4Instance("127.0.0.1", 10001)};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  std::list<TestListenerPtr> listeners_;
  std::unique_ptr<MockConnectionHandlerImpl> handler_;
  NiceMock<Network::MockFilterChainManager> manager_;
  NiceMock<Network::MockFilterChainFactory> factory_;
  const std::shared_ptr<Network::MockFilterChain> filter_chain_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  std::shared_ptr<NiceMock<Network::MockListenerFilterMatcher>> listener_filter_matcher_;
  bool udp_listener_deleted_ = false;
  bool deleted_before_listener_ = false;
  std::shared_ptr<AccessLog::MockInstance> access_log_;
  TestScopedRuntime scoped_runtime_;
  Runtime::Loader& runtime_{scoped_runtime_.loader()};
  testing::NiceMock<Random::MockRandomGenerator> random_;
};

// Verify that if a listener is removed while a rebalanced connection is in flight, we correctly
// destroy the connection.
TEST_F(ConnectionHandlerTest, RemoveListenerDuringRebalance) {
  InSequence s;

  // For reasons I did not investigate, the death test below requires this, likely due to
  // forking.
  // So we just leak the FDs for this test.
  ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  auto connection_balancer = std::make_shared<Network::MockConnectionBalancer>();
  Network::BalancedConnectionHandler* current_handler;
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, nullptr,
                  connection_balancer, &current_handler);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  // Fake a balancer posting a connection to us.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  current_handler->incNumConnections();
#ifndef NDEBUG
  EXPECT_CALL(*access_log_, log(_, _));
#endif
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
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 = addListener(1, false, false, "test_listener1", listener1,
                                             &listener_callbacks1, normal_address);
  // Only allow a single connection on this listener.
  test_listener1->setMaxConnections(1);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto listener2 = new NiceMock<Network::MockListener>();
  Network::TcpListenerCallbacks* listener_callbacks2;
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, &listener_callbacks2, alt_address);
  // Do not allow any connections on this listener.
  test_listener2->setMaxConnections(0);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

  EXPECT_CALL(manager_, findFilterChain(_, _)).WillRepeatedly(Return(filter_chain_.get()));
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
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks2->onAccept(
      Network::ConnectionSocketPtr{new NiceMock<Network::MockConnectionSocket>()});
  EXPECT_EQ(0, handler_->numConnections());
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(0, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "downstream_cx_overflow")->value());

  // For listener 1, verify connections are limited after one goes active.

  // First connection attempt should result in an active connection being created.
  auto conn1 = new NiceMock<Network::MockServerConnection>();
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

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);
}

TEST_F(ConnectionHandlerTest, RemoveListenerWithMultiAddrs) {
  InSequence s;

  auto listener1 = new NiceMock<Network::MockListener>();
  auto listener2 = new NiceMock<Network::MockListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr address1(
      new Network::Address::Ipv4Instance("127.0.0.1", 80, nullptr));
  Network::Address::InstanceConstSharedPtr address2(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(address1);
  addresses.emplace_back(address2);
  Network::TcpListenerCallbacks* listener_callbacks1;
  Network::TcpListenerCallbacks* listener_callbacks2;
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  listener_callbacks_map.emplace(address1->asString(), &listener_callbacks1);
  listener_callbacks_map.emplace(address2->asString(), &listener_callbacks2);
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;
  TestMultiAddressesListener* test_listener =
      addMultiAddrsListener(1, true, false, "test_listener", mock_listeners, addresses,
                            connection_balancers, listener_callbacks_map);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);

  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(1, {});
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList()).Times(2);
  handler_->removeListeners(1);
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);
}

TEST_F(ConnectionHandlerTest, DisableListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
}

TEST_F(ConnectionHandlerTest, DisableListenerWithMultiAddrs) {
  auto listener1 = new NiceMock<Network::MockListener>();
  auto listener2 = new NiceMock<Network::MockListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr address1(
      new Network::Address::Ipv4Instance("127.0.0.1", 80, nullptr));
  Network::Address::InstanceConstSharedPtr address2(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(address1);
  addresses.emplace_back(address2);
  Network::TcpListenerCallbacks* listener_callbacks1;
  Network::TcpListenerCallbacks* listener_callbacks2;
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  listener_callbacks_map.emplace(address1->asString(), &listener_callbacks1);
  listener_callbacks_map.emplace(address2->asString(), &listener_callbacks2);
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;
  TestMultiAddressesListener* test_listener =
      addMultiAddrsListener(1, false, false, "test_listener", mock_listeners, addresses,
                            connection_balancers, listener_callbacks_map);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(*listener1, disable());
  EXPECT_CALL(*listener2, disable());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());

  handler_->disableListeners();
}

TEST_F(ConnectionHandlerTest, RebalanceWithMultiAddressListener) {
  auto listener1 = new NiceMock<Network::MockListener>();
  auto listener2 = new NiceMock<Network::MockListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr address1(
      new Network::Address::Ipv4Instance("127.0.0.1", 80, nullptr));
  Network::Address::InstanceConstSharedPtr address2(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(address1);
  addresses.emplace_back(address2);

  Network::TcpListenerCallbacks* listener_callbacks1;
  Network::TcpListenerCallbacks* listener_callbacks2;
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  listener_callbacks_map.emplace(address1->asString(), &listener_callbacks1);
  listener_callbacks_map.emplace(address2->asString(), &listener_callbacks2);

  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;
  auto mock_connection_balancer1 = std::make_shared<Network::MockConnectionBalancer>();
  auto mock_connection_balancer2 = std::make_shared<Network::MockConnectionBalancer>();
  connection_balancers.emplace(address1->asString(), mock_connection_balancer1);
  connection_balancers.emplace(address2->asString(), mock_connection_balancer2);

  Network::BalancedConnectionHandler* current_handler1;
  Network::BalancedConnectionHandler* current_handler2;

  EXPECT_CALL(*mock_connection_balancer1, registerHandler(_))
      .WillOnce(SaveArgAddress(&current_handler1));
  EXPECT_CALL(*mock_connection_balancer2, registerHandler(_))
      .WillOnce(SaveArgAddress(&current_handler2));

  TestMultiAddressesListener* test_listener =
      addMultiAddrsListener(1, false, false, "test_listener", mock_listeners, addresses,
                            connection_balancers, listener_callbacks_map);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  // Send connection to the first listener, expect mock_connection_balancer1 will be called.
  // then mock_connection_balancer1 will balance the connection to the same listener.
  EXPECT_CALL(*mock_connection_balancer1, pickTargetHandler(_))
      .WillOnce(ReturnRef(*current_handler1));
  EXPECT_CALL(*access_log_, log(_, _));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  current_handler1->incNumConnections();
  listener_callbacks1->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());

  // Send connection to the second listener, expect mock_connection_balancer2 will be called.
  // then mock_connection_balancer2 will balance the connection to the same listener.
  EXPECT_CALL(*mock_connection_balancer2, pickTargetHandler(_))
      .WillOnce(ReturnRef(*current_handler2));
  EXPECT_CALL(*access_log_, log(_, _));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  current_handler2->incNumConnections();
  listener_callbacks2->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());

  EXPECT_CALL(*mock_connection_balancer1, unregisterHandler(_));
  EXPECT_CALL(*mock_connection_balancer2, unregisterHandler(_));
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
}

// Envoy doesn't have such case yet, just ensure the code won't break with it.
TEST_F(ConnectionHandlerTest, StopAndDisableStoppedListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});

  // Test stop a stopped listener.
  handler_->stopListeners(1, {});

  // Test disable a stopped listener.
  handler_->disableListeners();
}

TEST_F(ConnectionHandlerTest, AddDisabledListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*listener, disable());
  EXPECT_CALL(*listener, onDestroy());

  handler_->disableListeners();
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);
}

TEST_F(ConnectionHandlerTest, AddDisabledListenerWithMultiAddrs) {
  auto listener1 = new NiceMock<Network::MockListener>();
  auto listener2 = new NiceMock<Network::MockListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr address1(
      new Network::Address::Ipv4Instance("127.0.0.1", 80, nullptr));
  Network::Address::InstanceConstSharedPtr address2(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(address1);
  addresses.emplace_back(address2);
  Network::TcpListenerCallbacks* listener_callbacks1;
  Network::TcpListenerCallbacks* listener_callbacks2;
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  listener_callbacks_map.emplace(address1->asString(), &listener_callbacks1);
  listener_callbacks_map.emplace(address2->asString(), &listener_callbacks2);
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;
  TestMultiAddressesListener* test_listener =
      addMultiAddrsListener(1, false, false, "test_listener", mock_listeners, addresses,
                            connection_balancers, listener_callbacks_map, true);
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());

  handler_->disableListeners();
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);
}

TEST_F(ConnectionHandlerTest, SetListenerRejectFraction) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(*listener, setRejectFraction(UnitFloat(0.1234f)));
  EXPECT_CALL(*listener, onDestroy());

  handler_->setListenerRejectFraction(UnitFloat(0.1234f));
}

TEST_F(ConnectionHandlerTest, AddListenerSetRejectFraction) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);
  EXPECT_CALL(*listener, setRejectFraction(UnitFloat(0.12345f)));
  EXPECT_CALL(*listener, onDestroy());

  handler_->setListenerRejectFraction(UnitFloat(0.12345f));
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);
}

TEST_F(ConnectionHandlerTest, SetsTransportSocketConnectTimeout) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, false, false, "test_listener", listener, &listener_callbacks);

  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  auto server_connection = new NiceMock<Network::MockServerConnection>();

  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(server_connection));
  EXPECT_CALL(*filter_chain_, transportSocketConnectTimeout)
      .WillOnce(Return(std::chrono::seconds(5)));
  EXPECT_CALL(*server_connection,
              setTransportSocketConnectTimeout(std::chrono::milliseconds(5 * 1000), _));
  EXPECT_CALL(*access_log_, log(_, _));

  listener_callbacks->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, DestroyCloseConnections) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*listener, onDestroy());
  handler_.reset();
}

TEST_F(ConnectionHandlerTest, CloseDuringFilterChainCreate) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*connection, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  EXPECT_CALL(*access_log_, log(_, _));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, CloseConnectionOnEmptyFilterChain) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*connection, addConnectionCallbacks(_)).Times(0);
  EXPECT_CALL(*access_log_, log(_, _));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, NormalRedirect) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  Network::TcpListenerCallbacks* listener_callbacks2;
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002));
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, &listener_callbacks2, alt_address);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

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
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  // Verify per-listener connection stats.
  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*access_log_, log(_, _))
      .WillOnce(Invoke(
          [&](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(alt_address, stream_info.downstreamAddressProvider().localAddress());
          }));
  connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

TEST_F(ConnectionHandlerTest, NormalRedirectWithMultiAddrs) {
  auto listener1 = new NiceMock<Network::MockListener>();
  auto listener2 = new NiceMock<Network::MockListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001, nullptr));
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 20002, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(normal_address);
  addresses.emplace_back(alt_address);
  Network::TcpListenerCallbacks* listener_callbacks1;
  Network::TcpListenerCallbacks* listener_callbacks2;
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  listener_callbacks_map.emplace(normal_address->asString(), &listener_callbacks1);
  listener_callbacks_map.emplace(alt_address->asString(), &listener_callbacks2);
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;
  TestMultiAddressesListener* test_listener1 =
      addMultiAddrsListener(1, true, true, "test_listener1", mock_listeners, addresses,
                            connection_balancers, listener_callbacks_map);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

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
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  // Verify per-listener connection stats.
  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*access_log_, log(_, _))
      .WillOnce(Invoke(
          [&](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(alt_address, stream_info.downstreamAddressProvider().localAddress());
          }));
  connection->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
}

// When update a listener, the old listener will be stopped and the new listener will
// be added into ConnectionHandler before remove the old listener from ConnectionHandler.
// This test ensure ConnectionHandler can query the correct Listener when redirect the connection
// through `getBalancedHandlerByAddress`
TEST_F(ConnectionHandlerTest, MatchLatestListener) {
  Network::TcpListenerCallbacks* listener_callbacks;
  // The Listener1 will accept the new connection first then redirect to other listener.
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  // Listener2 will be replaced by Listener3.
  auto listener2_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr listener2_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002));
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, nullptr, listener2_address, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds(15000), false,
                  listener2_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

  // Listener3 will replace the listener2.
  auto listener3_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  auto listener3 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr listener3_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002));
  TestListener* test_listener3 =
      addListener(3, false, false, "test_listener3", listener3, nullptr, listener3_address, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds(15000), false,
                  listener3_overridden_filter_chain_manager);

  // This emulated the case of update listener in-place. Stop the old listener and
  // add the new listener.
  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2, {});
  handler_->addListener(absl::nullopt, *test_listener3, runtime_, random_);

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
  // This is the address of listener2 and listener3.
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // Ensure the request is going to the listener3.
  EXPECT_CALL(*listener3_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  // Ensure the request isn't going to the listener1 and listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  EXPECT_CALL(*listener2_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);

  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener3, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, EnsureNotMatchStoppedListener) {
  Network::TcpListenerCallbacks* listener_callbacks;
  // The Listener1 will accept the new connection first then redirect to other listener.
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto listener2_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr listener2_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002));
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, nullptr, listener2_address, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds(15000), false,
                  listener2_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

  // Stop the listener2.
  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2, {});

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
  // This is the address of listener2.
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // Ensure the request isn't going to the listener2.
  EXPECT_CALL(*listener2_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  // Ensure the request is going to the listener1.
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));

  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, EnsureNotMatchStoppedAnyAddressListener) {
  Network::TcpListenerCallbacks* listener_callbacks;
  // The Listener1 will accept the new connection first then redirect to other listener.
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto listener2_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr listener2_address(
      new Network::Address::Ipv4Instance("0.0.0.0", 10002));
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, nullptr, listener2_address, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds(15000), false,
                  listener2_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

  // Stop the listener2.
  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2, {});

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
  // This is the address of listener2.
  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10002, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // Ensure the request isn't going to the listener2.
  EXPECT_CALL(*listener2_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  // Ensure the request is going to the listener1.
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));

  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, FallbackToWildcardListener) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  Network::TcpListenerCallbacks* listener_callbacks2;
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getIpv4AnyAddress();
  TestListener* test_listener2 =
      addListener(2, false, false, "test_listener2", listener2, &listener_callbacks2, any_address);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

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
      new Network::Address::Ipv4Instance("127.0.0.2", 0, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, MatchIPv6WildcardListener) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto ipv4_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv4_any_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr any_address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80));
  TestListener* ipv4_any_listener = addListener(
      2, false, false, "ipv4_any_test_listener", listener2, &ipv4_any_listener_callbacks,
      any_address, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, ipv4_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv4_any_listener, runtime_, random_);

  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_any_listener_callbacks;
  auto listener3 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr any_address_ipv6(
      new Network::Address::Ipv6Instance("::", 80));
  TestListener* ipv6_any_listener = addListener(
      3, false, false, "ipv6_any_test_listener", listener3, &ipv6_any_listener_callbacks,
      any_address_ipv6, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_any_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv6Instance("::2", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  EXPECT_CALL(*ipv4_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener3, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This tests the ConnectionHandler's `getBalancedHandlerByAddress` will match
// Ipv4 request to the listener which is listening on "::" with ipv4_compat flag.
// The `listener1` is the listener will balance the request to the `listener2`,
// the listener2 is listening on IPv6 any-address. And suppose the `listener2`
// will accept the ipv4 request since it has ipv4_compat flag.
TEST_F(ConnectionHandlerTest, MatchIPv6WildcardListenerWithAnyAddressAndIpv4CompatFlag) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_any_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // Set the ipv6only as false.
  Network::Address::InstanceConstSharedPtr any_address_ipv6(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* ipv6_any_listener = addListener(
      2, false, false, "ipv6_any_test_listener", listener2, &ipv6_any_listener_callbacks,
      any_address_ipv6, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_any_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // The listener1 will balance the request to listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  // The listener2 gets the connection.
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This tests the ConnectionHandler's `getBalancedHandlerByAddress` will match
// Ipv4 request to the listener which is listening on Ipv4-compatible Ipv6 address with ipv4_compat
// flag. The `listener1` is the listener will balance the request to the `listener2`, the listener2
// is listening on Ipv4-compatible Ipv6 address. And suppose the `listener2` will accept the ipv4
// request since it has ipv4_compat flag.
TEST_F(ConnectionHandlerTest, MatchhIpv4CompatiableIPv6ListenerWithIpv4CompatFlag) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // Set the ipv6only as false.
  Network::Address::InstanceConstSharedPtr ipv4_mapped_ipv6_address(
      new Network::Address::Ipv6Instance("::FFFF:192.168.0.1", 80, nullptr, false));
  TestListener* ipv6_listener =
      addListener(2, false, false, "ipv6_test_listener", listener2, &ipv6_listener_callbacks,
                  ipv4_mapped_ipv6_address, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("192.168.0.1", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // The listener1 will balance the request to listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  // The listener2 gets the connection.
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This tests the ConnectionHandler's `getBalancedHandlerByAddress` won't match
// Ipv4 request to the listener which is listening on Ipv6 any-address without ipv4_compat flag.
// The `listener1` is the listener will balance the request to the `listener2`,
// the listener2 is listening on IPv6 any-address. And suppose the `listener2`
// will accept the Ipv4 request since it has ipv4_compat flag.
TEST_F(ConnectionHandlerTest, NotMatchIPv6WildcardListenerWithoutIpv4CompatFlag) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_any_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // not set the ipv6only flag, the default value is true.
  Network::Address::InstanceConstSharedPtr any_address_ipv6(
      new Network::Address::Ipv6Instance("::", 80));
  TestListener* ipv6_any_listener = addListener(
      2, false, false, "ipv6_any_test_listener", listener2, &ipv6_any_listener_callbacks,
      any_address_ipv6, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_any_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // The listener1 gets the connection.
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  // The listener2 doesn't get the connection.
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This tests the case both "0.0.0.0" and "::" with ipv4_compat are added. The
// expectation is the ipv4 connection is going to ipv4 listener.
TEST_F(ConnectionHandlerTest, MatchhIpv4WhenBothIpv4AndIPv6WithIpv4CompatFlag) {
  // Listener1 is response for redirect the connection.
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  // Listener2 is listening on an ipv4-mapped ipv6 address.
  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_any_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // Set the ipv6only as false.
  Network::Address::InstanceConstSharedPtr ipv6_any_address(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* ipv6_listener =
      addListener(2, false, false, "ipv6_test_listener", listener2, &ipv6_any_listener_callbacks,
                  ipv6_any_address, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_listener, runtime_, random_);

  // Listener3 is listening on an ipv4 address.
  auto ipv4_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv4_listener_callbacks;
  auto listener3 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr ipv4_address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80, nullptr));
  TestListener* ipv4_listener =
      addListener(3, false, false, "ipv4_test_listener", listener3, &ipv4_listener_callbacks,
                  ipv4_address, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, ipv4_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv4_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("192.168.0.1", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // The listener1 will balance the request to listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  // The listener2 won't get the connection.
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  // The listener3 gets the connection.
  EXPECT_CALL(*ipv4_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener3, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This test is same as above except the Ipv4 listener is added first, then Ipv6
// listener is added.
TEST_F(ConnectionHandlerTest, MatchhIpv4WhenBothIpv4AndIPv6WithIpv4CompatFlag2) {
  // Listener1 is response for redirect the connection.
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  // Listener3 is listening on an ipv4 address.
  auto ipv4_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv4_listener_callbacks;
  auto listener3 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr ipv4_address(
      new Network::Address::Ipv4Instance("0.0.0.0", 80, nullptr));
  TestListener* ipv4_listener =
      addListener(3, false, false, "ipv4_test_listener", listener3, &ipv4_listener_callbacks,
                  ipv4_address, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, ipv4_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv4_listener, runtime_, random_);

  // Listener2 is listening on an ipv4-mapped ipv6 address.
  auto ipv6_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* ipv6_any_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // Set the ipv6only as false.
  Network::Address::InstanceConstSharedPtr ipv4_mapped_ipv6_address(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* ipv6_listener =
      addListener(2, false, false, "ipv6_test_listener", listener2, &ipv6_any_listener_callbacks,
                  ipv4_mapped_ipv6_address, nullptr, nullptr, Network::Socket::Type::Stream,
                  std::chrono::milliseconds(15000), false, ipv6_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *ipv6_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("192.168.0.1", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));
  // The listener1 will balance the request to listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  // The listener2 won't get the connection.
  EXPECT_CALL(*ipv6_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  // The listener3 gets the connection.
  EXPECT_CALL(*ipv4_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener3, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

// This test the case of an update for listener which listening
// on an Ipv4-mapped Ipv6 address.
TEST_F(ConnectionHandlerTest, UpdateIpv4MappedListener) {
  // Listener1 is response for redirect the connection.
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 10001));
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, normal_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

  // Listener2 is listening on an Ipv4-mapped Ipv6 address.
  auto listener2_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* listener2_listener_callbacks;
  auto listener2 = new NiceMock<Network::MockListener>();
  // Set the ipv6only as false.
  Network::Address::InstanceConstSharedPtr ipv4_mapped_ipv6_address(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* origin_ipv4_mapped_listener = addListener(
      2, false, false, "ipv4_mapped_test_listener", listener2, &listener2_listener_callbacks,
      ipv4_mapped_ipv6_address, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, listener2_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *origin_ipv4_mapped_listener, runtime_, random_);

  // Listener3 is an update of listener2.
  auto listener3_overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  Network::TcpListenerCallbacks* listener3_any_listener_callbacks;
  auto listener3 = new NiceMock<Network::MockListener>();
  TestListener* updated_ipv4_mapped_listener = addListener(
      3, false, false, "ipv4_mapped_test_listener", listener3, &listener3_any_listener_callbacks,
      ipv4_mapped_ipv6_address, nullptr, nullptr, Network::Socket::Type::Stream,
      std::chrono::milliseconds(15000), false, listener3_overridden_filter_chain_manager);
  handler_->addListener(absl::nullopt, *updated_ipv4_mapped_listener, runtime_, random_);

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

  Network::Address::InstanceConstSharedPtr alt_address(
      new Network::Address::Ipv4Instance("192.168.0.1", 80, nullptr));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks& cb) -> Network::FilterStatus {
        cb.socket().connectionInfoProvider().restoreLocalAddress(alt_address);
        return Network::FilterStatus::Continue;
      }));

  // The listener1 will balance the request to listener2.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  // The listener2 won't get the connection since it is override by the listener3.
  EXPECT_CALL(*listener2_overridden_filter_chain_manager, findFilterChain(_, _)).Times(0);
  // The listener3 gets the connection.
  EXPECT_CALL(*listener3_overridden_filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(filter_chain_.get()));

  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener3, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithOriginalDstInbound) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);
  test_listener1->setDirection(envoy::config::core::v3::TrafficDirection::INBOUND);
  validateOriginalDst(&listener_callbacks1, test_listener1, listener1);
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithOriginalDstOutbound) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1);
  test_listener1->setDirection(envoy::config::core::v3::TrafficDirection::OUTBOUND);
  validateOriginalDst(&listener_callbacks1, test_listener1, listener1);
}

TEST_F(ConnectionHandlerTest, WildcardListenerWithNoOriginalDst) {
  Network::TcpListenerCallbacks* listener_callbacks1;
  auto listener1 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr normal_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 80));
  Network::Address::InstanceConstSharedPtr any_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getIpv4AnyAddress(), normal_address->ip()->port());
  TestListener* test_listener1 =
      addListener(1, true, true, "test_listener1", listener1, &listener_callbacks1, any_address);
  handler_->addListener(absl::nullopt, *test_listener1, runtime_, random_);

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
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  listener_callbacks1->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  EXPECT_EQ(1UL, handler_->numConnections());

  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*access_log_, log(_, _));
}

TEST_F(ConnectionHandlerTest, TransportProtocolDefault) {
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*accepted_socket, detectedTransportProtocol())
      .WillOnce(Return(absl::string_view("")));
  EXPECT_CALL(*accepted_socket, setDetectedTransportProtocol(absl::string_view("raw_buffer")));
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TransportProtocolCustom) {
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

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
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

// Timeout during listener filter stop iteration.
TEST_F(ConnectionHandlerTest, ListenerFilterTimeout) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter(512);
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
  Network::MockIoHandle io_handle;
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  EXPECT_CALL(io_handle,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed));
  EXPECT_CALL(io_handle, activateFileEvents(Event::FileReadyType::Read));
  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  Stats::Gauge& downstream_pre_cx_active =
      stats_store_.gauge("downstream_pre_cx_active", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1UL, downstream_pre_cx_active.value());

  EXPECT_CALL(*timeout, disableTimer());
  EXPECT_CALL(*access_log_, log(_, _));
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

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, nullptr, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds(15000), true);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockListenerFilter* test_filter = new NiceMock<Network::MockListenerFilter>(128);
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));

  std::string data = "test";
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  Network::IoSocketHandleImpl io_handle{42};
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  Event::FileEvent* file_event = new NiceMock<Event::MockFileEvent>();
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event));

  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});
  Stats::Gauge& downstream_pre_cx_active =
      stats_store_.gauge("downstream_pre_cx_active", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1UL, downstream_pre_cx_active.value());
  EXPECT_CALL(*test_filter, destroy_());
  // Barrier: test_filter must be destructed before findFilterChain
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  EXPECT_CALL(*timeout, disableTimer());
  timeout->invokeCallback();
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0UL, downstream_pre_cx_active.value());
  EXPECT_EQ(1UL, stats_store_.counter("downstream_pre_cx_timeout").value());

  // Make sure we continued to try create connection.
  EXPECT_EQ(1UL, stats_store_.counter("no_filter_chain_match").value());

  Event::FileEvent* file_event2 = new NiceMock<Event::MockFileEvent>();
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event2));
  EXPECT_CALL(*listener, onDestroy());

  // Verify the file event created by listener filter was reset. If not
  // the initializeFileEvent will trigger the assertion.
  io_handle.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read | Event::FileReadyType::Closed);
}

// Timeout is disabled once the listener filters complete.
TEST_F(ConnectionHandlerTest, ListenerFilterTimeoutResetOnSuccess) {
  InSequence s;

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);
  size_t max_size = 10;
  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter(max_size);
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockIoHandle io_handle;
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle));
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle));

  Event::FileReadyCb file_event_callback;
  EXPECT_CALL(io_handle,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed))
      .WillOnce(SaveArg<1>(&file_event_callback));
  EXPECT_CALL(io_handle, activateFileEvents(_));

  Event::MockTimer* timeout = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*timeout, enableTimer(std::chrono::milliseconds(15000), _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(io_handle, recv(_, _, _))
      .WillOnce(Return(ByMove(Api::IoCallUint64Result(max_size, Api::IoError::none()))));
  EXPECT_CALL(*test_filter, onData(_))
      .WillOnce(Invoke([](Network::ListenerFilterBuffer&) -> Network::FilterStatus {
        return Network::FilterStatus::Continue;
      }));
  EXPECT_CALL(io_handle, resetFileEvents());
  EXPECT_CALL(*test_filter, destroy_());
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  EXPECT_CALL(*timeout, disableTimer());

  ASSERT_TRUE(file_event_callback(Event::FileReadyType::Read).ok());

  EXPECT_CALL(io_handle, createFileEvent_(_, _, _, _));
  EXPECT_CALL(*listener, onDestroy());

  // Verify the file event created by listener filter was reset. If not
  // the initializeFileEvent will trigger the assertion.
  io_handle.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read | Event::FileReadyType::Closed);
}

// Ensure there is no timeout when the timeout is disabled with 0s.
TEST_F(ConnectionHandlerTest, ListenerFilterDisabledTimeout) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, nullptr, nullptr,
                  nullptr, Network::Socket::Type::Stream, std::chrono::milliseconds());
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockListenerFilter* test_filter = new Network::MockListenerFilter(512);
  EXPECT_CALL(factory_, createListenerFilterChain(_))
      .WillRepeatedly(Invoke([&](Network::ListenerFilterManager& manager) -> bool {
        manager.addAcceptFilter(listener_filter_matcher_, Network::ListenerFilterPtr{test_filter});
        return true;
      }));
  EXPECT_CALL(*test_filter, onAccept(_))
      .WillOnce(Invoke([&](Network::ListenerFilterCallbacks&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  Network::MockIoHandle io_handle;
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  EXPECT_CALL(io_handle, isOpen()).WillOnce(Return(true));
  EXPECT_CALL(*accepted_socket, ioHandle()).WillOnce(ReturnRef(io_handle)).RetiresOnSaturation();
  EXPECT_CALL(io_handle,
              createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                               Event::FileReadyType::Read | Event::FileReadyType::Closed));
  EXPECT_CALL(io_handle, activateFileEvents(Event::FileReadyType::Read));
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*test_filter, destroy_());
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  EXPECT_CALL(*listener, onDestroy());
}

// Listener Filter could close socket in the context of listener callback.
TEST_F(ConnectionHandlerTest, ListenerFilterReportError) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

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
  EXPECT_CALL(*access_log_, log(_, _));
  // The last filter won't be invoked
  EXPECT_CALL(*last_filter, onAccept(_)).Times(0);
  EXPECT_CALL(*first_filter, destroy_());
  EXPECT_CALL(*last_filter, destroy_());
  Network::MockConnectionSocket* accepted_socket = new NiceMock<Network::MockConnectionSocket>();
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{accepted_socket});

  dispatcher_.clearDeferredDeleteList();
  // Make sure the error leads to no listener timer created.
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  // Make sure we never try to match the filter chain since listener filter doesn't complete.
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);

  EXPECT_CALL(*listener, onDestroy());
}

// Ensure no filters registered for a UDP listener is handled correctly.
TEST_F(ConnectionHandlerTest, UdpListenerNoFilter) {
  InSequence s;

  auto listener = new NiceMock<Network::MockUdpListener>();
  EXPECT_CALL(*listener, onDestroy());
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, nullptr, nullptr, nullptr, nullptr,
                  Network::Socket::Type::Datagram, std::chrono::milliseconds());
  EXPECT_CALL(test_listener->socketFactory(), localAddress())
      .WillRepeatedly(ReturnRef(local_address_));

  Network::UdpListenerCallbacks* callbacks = nullptr;
  auto udp_listener_worker_router = static_cast<Network::MockUdpListenerWorkerRouter*>(
      test_listener->udp_listener_config_->listener_worker_router_map_
          .find(local_address_->asString())
          ->second.get());
  EXPECT_CALL(*udp_listener_worker_router, registerWorkerForListener(_))
      .WillOnce(Invoke([&](Network::UdpListenerCallbacks& cb) {
        EXPECT_CALL(*udp_listener_worker_router, unregisterWorkerForListener(_));
        callbacks = &cb;
      }));

  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  // Make sure these calls don't crash.
  Network::UdpRecvData data;
  callbacks->onData(std::move(data));
  callbacks->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(ConnectionHandlerTest, UdpListenerWorkerRouterWithMultipleAddresses) {
  auto listener1 = new NiceMock<Network::MockUdpListener>();
  auto listener2 = new NiceMock<Network::MockUdpListener>();
  std::vector<Network::Listener*> mock_listeners;
  mock_listeners.emplace_back(listener1);
  mock_listeners.emplace_back(listener2);
  Network::Address::InstanceConstSharedPtr address1(
      new Network::Address::Ipv4Instance("127.0.0.1", 80, nullptr));
  Network::Address::InstanceConstSharedPtr address2(
      new Network::Address::Ipv4Instance("127.0.0.2", 80, nullptr));
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  addresses.emplace_back(address1);
  addresses.emplace_back(address2);

  // Using empty map here since those are useless for UDP.
  absl::flat_hash_map<std::string, Network::TcpListenerCallbacks**> listener_callbacks_map;
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers;

  TestMultiAddressesListener* test_listener = addMultiAddrsListener(
      1, false, false, "test_listener", mock_listeners, addresses, connection_balancers,
      listener_callbacks_map, false, Network::Socket::Type::Datagram);

  auto udp_listener_worker_router1 = static_cast<Network::MockUdpListenerWorkerRouter*>(
      test_listener->udp_listener_config_->listener_worker_router_map_.find(address1->asString())
          ->second.get());
  auto udp_listener_worker_router2 = static_cast<Network::MockUdpListenerWorkerRouter*>(
      test_listener->udp_listener_config_->listener_worker_router_map_.find(address2->asString())
          ->second.get());
  EXPECT_CALL(*udp_listener_worker_router1, registerWorkerForListener(_));
  EXPECT_CALL(*udp_listener_worker_router2, registerWorkerForListener(_));

  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  EXPECT_CALL(*udp_listener_worker_router1, unregisterWorkerForListener(_));
  EXPECT_CALL(*udp_listener_worker_router2, unregisterWorkerForListener(_));
  EXPECT_CALL(*listener1, onDestroy());
  EXPECT_CALL(*listener2, onDestroy());
  delete mock_listeners[0];
  delete mock_listeners[1];
}

TEST_F(ConnectionHandlerTest, TcpListenerInplaceUpdate) {
  InSequence s;
  uint64_t old_listener_tag = 1;
  uint64_t new_listener_tag = 2;
  Network::TcpListenerCallbacks* old_listener_callbacks;
  Network::BalancedConnectionHandler* current_handler;

  auto old_listener = new NiceMock<Network::MockListener>();
  auto mock_connection_balancer = std::make_shared<Network::MockConnectionBalancer>();

  TestListener* old_test_listener =
      addListener(old_listener_tag, true, false, "test_listener", old_listener,
                  &old_listener_callbacks, nullptr, mock_connection_balancer, &current_handler);
  handler_->addListener(absl::nullopt, *old_test_listener, runtime_, random_);
  ASSERT_NE(old_test_listener, nullptr);

  Network::TcpListenerCallbacks* new_listener_callbacks = nullptr;

  auto overridden_filter_chain_manager =
      std::make_shared<NiceMock<Network::MockFilterChainManager>>();
  TestListener* new_test_listener =
      addListener(new_listener_tag, true, false, "test_listener", /* Network::Listener */ nullptr,
                  &new_listener_callbacks, nullptr, mock_connection_balancer, nullptr,
                  Network::Socket::Type::Stream, std::chrono::milliseconds(15000), false,
                  overridden_filter_chain_manager);
  handler_->addListener(old_listener_tag, *new_test_listener, runtime_, random_);
  ASSERT_EQ(new_listener_callbacks, nullptr)
      << "new listener should be inplace added and callback should not change";

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  current_handler->incNumConnections();

  EXPECT_CALL(*mock_connection_balancer, pickTargetHandler(_))
      .WillOnce(ReturnRef(*current_handler));
  EXPECT_CALL(manager_, findFilterChain(_, _)).Times(0);
  EXPECT_CALL(*overridden_filter_chain_manager, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  EXPECT_CALL(*mock_connection_balancer, unregisterHandler(_));
  old_listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());
  EXPECT_CALL(*old_listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveFilterChain) {
  InSequence s;
  uint64_t listener_tag = 1;
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(listener_tag, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* server_connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(server_connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*access_log_, log(_, _));

  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});

  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  const std::list<const Network::FilterChain*> filter_chains{filter_chain_.get()};

  // The completion callback is scheduled.
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

// `removeListeners` and `removeFilterChains` are posted from main thread. The two post actions are
// triggered by two timers. In some corner cases, the two timers have the same expiration time
// point. Thus `removeListeners` may be executed prior to `removeFilterChains`. This test case
// verifies that the work threads remove the listener and filter chains successfully under the above
// sequence.
TEST_F(ConnectionHandlerTest, TcpListenerRemoveFilterChainCalledAfterListenerIsRemoved) {
  InSequence s;
  uint64_t listener_tag = 1;
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(listener_tag, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(filter_chain_.get()));
  auto* server_connection = new NiceMock<Network::MockServerConnection>();
  EXPECT_CALL(dispatcher_, createServerConnection_()).WillOnce(Return(server_connection));
  EXPECT_CALL(factory_, createNetworkFilterChain(_, _)).WillOnce(Return(true));

  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});

  EXPECT_EQ(1UL, handler_->numConnections());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(1UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(listener_tag, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  EXPECT_CALL(*access_log_, log(_, _));

  {
    // Filter chain removal in the same poll cycle but earlier.
    handler_->removeListeners(listener_tag);
  }
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  const std::list<const Network::FilterChain*> filter_chains{filter_chain_.get()};
  MockFunction<void()> on_filter_chain_removed;
  {
    // Listener removal in the same poll cycle but later than filter chain removal.
    handler_->removeFilterChains(listener_tag, filter_chains,
                                 [&on_filter_chain_removed]() { on_filter_chain_removed.Call(); });
  }
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  // on_filter_chain_removed must be deferred called.
  EXPECT_CALL(on_filter_chain_removed, Call());
  dispatcher_.clearDeferredDeleteList();

  // Final counters.
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_total")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "downstream_cx_active")->value());
  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "test.downstream_cx_total")->value());
  EXPECT_EQ(0UL, TestUtility::findGauge(stats_store_, "test.downstream_cx_active")->value());

  // Verify that the callback is invoked already.
  testing::Mock::VerifyAndClearExpectations(&on_filter_chain_removed);
  handler_.reset();
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);
  EXPECT_EQ(0UL, handler_->numConnections());

  // Test stop/remove of not existent listener.
  handler_->stopListeners(0, {});
  handler_->removeListeners(0);
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveIpv6AnyAddressWithIpv4CompatListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr any_address_ipv6(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, any_address_ipv6);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);

  // Ensure both the Ipv6 and Ipv4 address was removed.
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*any_address_ipv6).has_value());
  Network::Address::InstanceConstSharedPtr any_address_ipv4(
      new Network::Address::Ipv4Instance("0.0.0.0", 80, nullptr));
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*any_address_ipv4).has_value());
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveIpv4CompatAddressListener) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr address_ipv6(
      new Network::Address::Ipv6Instance("::FFFF:192.168.0.1", 80, nullptr, false));
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, address_ipv6);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);

  // Ensure both the ipv6 and ipv4 address was removed.
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*address_ipv6).has_value());
  Network::Address::InstanceConstSharedPtr address_ipv4(
      new Network::Address::Ipv4Instance("192.168.0.1", 80, nullptr));
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*address_ipv4).has_value());
}

TEST_F(ConnectionHandlerTest, TcpListenerRemoveWithBothIpv4AnyAndIpv6Any) {
  InSequence s;

  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr address_ipv6(
      new Network::Address::Ipv6Instance("::", 80, nullptr, false));
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks, address_ipv6);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  Network::TcpListenerCallbacks* listener_callbacks2;
  auto listener2 = new NiceMock<Network::MockListener>();
  Network::Address::InstanceConstSharedPtr address_ipv4(
      new Network::Address::Ipv4Instance("0.0.0.0", 80, nullptr));
  TestListener* test_listener2 =
      addListener(2, true, false, "test_listener2", listener2, &listener_callbacks2, address_ipv4);
  handler_->addListener(absl::nullopt, *test_listener2, runtime_, random_);

  Network::MockConnectionSocket* connection = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(Network::ConnectionSocketPtr{connection});
  EXPECT_EQ(0UL, handler_->numConnections());

  Network::MockConnectionSocket* connection2 = new NiceMock<Network::MockConnectionSocket>();
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks2->onAccept(Network::ConnectionSocketPtr{connection2});
  EXPECT_EQ(0UL, handler_->numConnections());

  // Remove Listener1 first.
  EXPECT_CALL(*listener, onDestroy());
  handler_->stopListeners(1, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(1);

  // Ensure only ipv6 address was removed.
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*address_ipv6).has_value());
  EXPECT_TRUE(handler_->getBalancedHandlerByAddress(*address_ipv4).has_value());

  // Now remove Listener2.
  EXPECT_CALL(*listener2, onDestroy());
  handler_->stopListeners(2, {});

  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  handler_->removeListeners(2);
  // Ensure the listener2 is gone.
  EXPECT_FALSE(handler_->getBalancedHandlerByAddress(*address_ipv4).has_value());
}

TEST_F(ConnectionHandlerTest, TcpListenerGlobalCxLimitReject) {
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  listener_callbacks->onReject(Network::TcpListenerCallbacks::RejectCause::GlobalCxLimit);

  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_global_cx_overflow")->value());
  EXPECT_EQ(0UL, TestUtility::findCounter(stats_store_, "downstream_cx_overload_reject")->value());
  EXPECT_CALL(*listener, onDestroy());
}

TEST_F(ConnectionHandlerTest, TcpListenerOverloadActionReject) {
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

  listener_callbacks->onReject(Network::TcpListenerCallbacks::RejectCause::OverloadAction);

  EXPECT_EQ(1UL, TestUtility::findCounter(stats_store_, "downstream_cx_overload_reject")->value());
  EXPECT_EQ(0UL, TestUtility::findCounter(stats_store_, "downstream_global_cx_overflow")->value());
  EXPECT_CALL(*listener, onDestroy());
}

// Listener Filter matchers works.
TEST_F(ConnectionHandlerTest, ListenerFilterWorks) {
  Network::TcpListenerCallbacks* listener_callbacks;
  auto listener = new NiceMock<Network::MockListener>();
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, &listener_callbacks);
  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);

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
  EXPECT_CALL(manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*access_log_, log(_, _));
  listener_callbacks->onAccept(std::make_unique<NiceMock<Network::MockConnectionSocket>>());
  EXPECT_CALL(*listener, onDestroy());
}

// Tests shutdown does not cause problems.
TEST_F(ConnectionHandlerTest, ShutdownUdpListener) {
  InSequence s;

  Network::MockUdpReadFilterCallbacks dummy_callbacks;
  auto listener = new NiceMock<MockUpstreamUdpListener>(*this);
  TestListener* test_listener =
      addListener(1, true, false, "test_listener", listener, nullptr, nullptr, nullptr, nullptr,
                  Network::Socket::Type::Datagram, std::chrono::milliseconds(), false, nullptr);
  auto filter = std::make_unique<NiceMock<MockUpstreamUdpFilter>>(*this, dummy_callbacks);

  EXPECT_CALL(factory_, createUdpListenerFilterChain(_, _))
      .WillOnce(Invoke([&](Network::UdpListenerFilterManager& udp_listener,
                           Network::UdpReadFilterCallbacks&) -> bool {
        udp_listener.addReadFilter(std::move(filter));
        return true;
      }));
  EXPECT_CALL(test_listener->socketFactory(), localAddress())
      .WillRepeatedly(ReturnRef(local_address_));
  EXPECT_CALL(dummy_callbacks.udp_listener_, onDestroy());

  handler_->addListener(absl::nullopt, *test_listener, runtime_, random_);
  handler_->stopListeners();
}

} // namespace
} // namespace Server
} // namespace Envoy
