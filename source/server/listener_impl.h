#pragma once

#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/common/basic_resource_impl.h"
#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/server/filter_chain_manager_impl.h"

namespace Envoy {
namespace Server {

class ListenerMessageUtil {
public:
  /**
   * @return true if listener message lhs and rhs are the same if ignoring filter_chains field.
   */
  static bool filterChainOnlyChange(const envoy::config::listener::v3::Listener& lhs,
                                    const envoy::config::listener::v3::Listener& rhs);
};

class ListenerManagerImpl;

class ListenSocketFactoryImpl : public Network::ListenSocketFactory,
                                protected Logger::Loggable<Logger::Id::config> {
public:
  ListenSocketFactoryImpl(ListenerComponentFactory& factory,
                          Network::Address::InstanceConstSharedPtr address,
                          Network::Socket::Type socket_type,
                          const Network::Socket::OptionsSharedPtr& options,
                          const std::string& listener_name, uint32_t tcp_backlog_size,
                          ListenerComponentFactory::BindType bind_type, uint32_t num_sockets);

  // Network::ListenSocketFactory
  Network::Socket::Type socketType() const override { return socket_type_; }
  const Network::Address::InstanceConstSharedPtr& localAddress() const override {
    return local_address_;
  }
  Network::SocketSharedPtr getListenSocket(uint32_t worker_index) override;
  Network::ListenSocketFactoryPtr clone() const override {
    return absl::WrapUnique(new ListenSocketFactoryImpl(*this));
  }
  void closeAllSockets() override {
    for (auto& socket : sockets_) {
      socket->close();
    }
  }
  void doFinalPreWorkerInit() override;

private:
  ListenSocketFactoryImpl(const ListenSocketFactoryImpl& factory_to_clone);

  Network::SocketSharedPtr createListenSocketAndApplyOptions(ListenerComponentFactory& factory,
                                                             Network::Socket::Type socket_type,
                                                             uint32_t worker_index);

  ListenerComponentFactory& factory_;
  // Initially, its port number might be 0. Once a socket is created, its port
  // will be set to the binding port.
  Network::Address::InstanceConstSharedPtr local_address_;
  const Network::Socket::Type socket_type_;
  const Network::Socket::OptionsSharedPtr options_;
  const std::string listener_name_;
  const uint32_t tcp_backlog_size_;
  ListenerComponentFactory::BindType bind_type_;
  // One socket for each worker, pre-created before the workers fetch the sockets. There are
  // 3 different cases:
  // 1) All are null when doing config validation.
  // 2) A single socket has been duplicated for each worker (no reuse_port).
  // 3) A unique socket for each worker (reuse_port).
  //
  // TODO(mattklein123): If a listener does not bind, it still has a socket. This is confusing
  // and not needed and can be cleaned up.
  std::vector<Network::SocketSharedPtr> sockets_;
};

// TODO(mattklein123): Consider getting rid of pre-worker start and post-worker start code by
//                     initializing all listeners after workers are started.

/**
 * The common functionality shared by PerListenerFilterFactoryContexts and
 * PerFilterChainFactoryFactoryContexts.
 */
class ListenerFactoryContextBaseImpl final : public Configuration::FactoryContext,
                                             public Network::DrainDecision {
public:
  ListenerFactoryContextBaseImpl(Envoy::Server::Instance& server,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 const envoy::config::listener::v3::Listener& config,
                                 Server::DrainManagerPtr drain_manager);
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  const Server::Options& options() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Router::Context& routerContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::Instance& threadLocal() override;
  Admin& admin() override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  Stats::Scope& listenerScope() override;
  bool isQuicListener() const override;

  // DrainDecision
  bool drainClose() const override {
    return drain_manager_->drainClose() || server_.drainManager().drainClose();
  }
  Common::CallbackHandlePtr addOnDrainCloseCb(DrainCloseCb) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
    return nullptr;
  }
  Server::DrainManager& drainManager();

private:
  Envoy::Server::Instance& server_;
  const envoy::config::core::v3::Metadata metadata_;
  envoy::config::core::v3::TrafficDirection direction_;
  Stats::ScopePtr global_scope_;
  Stats::ScopePtr listener_scope_; // Stats with listener named scope.
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const Server::DrainManagerPtr drain_manager_;
  bool is_quic_;
};

class ListenerImpl;

// TODO(lambdai): Strip the interface since ListenerFactoryContext only need to support
// ListenerFilterChain creation. e.g, Is listenerMetaData() required? Is it required only at
// listener update or during the lifetime of listener?
class PerListenerFactoryContextImpl : public Configuration::ListenerFactoryContext {
public:
  PerListenerFactoryContextImpl(Envoy::Server::Instance& server,
                                ProtobufMessage::ValidationVisitor& validation_visitor,
                                const envoy::config::listener::v3::Listener& config_message,
                                const Network::ListenerConfig* listener_config,
                                ListenerImpl& listener_impl, DrainManagerPtr drain_manager)
      : listener_factory_context_base_(std::make_shared<ListenerFactoryContextBaseImpl>(
            server, validation_visitor, config_message, std::move(drain_manager))),
        listener_config_(listener_config), listener_impl_(listener_impl) {}
  PerListenerFactoryContextImpl(
      std::shared_ptr<ListenerFactoryContextBaseImpl> listener_factory_context_base,
      const Network::ListenerConfig* listener_config, ListenerImpl& listener_impl)
      : listener_factory_context_base_(listener_factory_context_base),
        listener_config_(listener_config), listener_impl_(listener_impl) {}

  // FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  const Options& options() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Router::Context& routerContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::Instance& threadLocal() override;
  Admin& admin() override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;

  Stats::Scope& listenerScope() override;
  bool isQuicListener() const override;

  // ListenerFactoryContext
  const Network::ListenerConfig& listenerConfig() const override;

  ListenerFactoryContextBaseImpl& parentFactoryContext() { return *listener_factory_context_base_; }
  friend class ListenerImpl;

private:
  std::shared_ptr<ListenerFactoryContextBaseImpl> listener_factory_context_base_;
  const Network::ListenerConfig* listener_config_;
  ListenerImpl& listener_impl_;
};

/**
 * Maps proto config to runtime config for a listener with a network filter chain.
 */
class ListenerImpl final : public Network::ListenerConfig,
                           public Network::FilterChainFactory,
                           Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Create a new listener.
   * @param config supplies the configuration proto.
   * @param version_info supplies the xDS version of the listener.
   * @param parent supplies the owning manager.
   * @param name supplies the listener name.
   * @param added_via_api supplies whether the listener can be updated or removed.
   * @param workers_started supplies whether the listener is being added before or after workers
   *        have been started. This controls various behavior related to init management.
   * @param hash supplies the hash to use for duplicate checking.
   * @param concurrency is the number of listeners instances to be created.
   */
  ListenerImpl(const envoy::config::listener::v3::Listener& config, const std::string& version_info,
               ListenerManagerImpl& parent, const std::string& name, bool added_via_api,
               bool workers_started, uint64_t hash, uint32_t concurrency);
  ~ListenerImpl() override;

  // TODO(lambdai): Explore using the same ListenerImpl object to execute in place filter chain
  // update.
  /**
   * Execute in place filter chain update. The filter chain update is less expensive than full
   * listener update because connections may not need to be drained.
   */
  std::unique_ptr<ListenerImpl>
  newListenerWithFilterChain(const envoy::config::listener::v3::Listener& config,
                             bool workers_started, uint64_t hash);
  /**
   * Determine if in place filter chain update could be executed at this moment.
   */
  bool supportUpdateFilterChain(const envoy::config::listener::v3::Listener& config,
                                bool worker_started);

  /**
   * Run the callback on each filter chain that exists in this listener but not in the passed
   * listener config.
   */
  void diffFilterChain(const ListenerImpl& another_listener,
                       std::function<void(Network::DrainableFilterChain&)> callback);

  /**
   * Helper functions to determine whether a listener is blocked for update or remove.
   */
  bool blockUpdate(uint64_t new_hash) { return new_hash == hash_ || !added_via_api_; }
  bool blockRemove() { return !added_via_api_; }

  Network::Address::InstanceConstSharedPtr address() const { return address_; }
  const envoy::config::listener::v3::Listener& config() const { return config_; }
  const Network::ListenSocketFactory& getSocketFactory() const { return *socket_factory_; }
  void debugLog(const std::string& message);
  void initialize();
  DrainManager& localDrainManager() const {
    return listener_factory_context_->listener_factory_context_base_->drainManager();
  }
  void setSocketFactory(Network::ListenSocketFactoryPtr&& socket_factory);
  void setSocketAndOptions(const Network::SocketSharedPtr& socket);
  const Network::Socket::OptionsSharedPtr& listenSocketOptions() { return listen_socket_options_; }
  const std::string& versionInfo() const { return version_info_; }
  bool reusePort() const { return reuse_port_; }
  static bool getReusePortOrDefault(Server::Instance& server,
                                    const envoy::config::listener::v3::Listener& config);

  // Check whether a new listener can share sockets with this listener.
  bool hasCompatibleAddress(const ListenerImpl& other) const;

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return filter_chain_manager_; }
  Network::FilterChainFactory& filterChainFactory() override { return *this; }
  Network::ListenSocketFactory& listenSocketFactory() override { return *socket_factory_; }
  bool bindToPort() override { return bind_to_port_; }
  bool handOffRestoredDestinationConnections() const override {
    return hand_off_restored_destination_connections_;
  }
  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }
  std::chrono::milliseconds listenerFiltersTimeout() const override {
    return listener_filters_timeout_;
  }
  bool continueOnListenerFiltersTimeout() const override {
    return continue_on_listener_filters_timeout_;
  }
  Stats::Scope& listenerScope() override { return listener_factory_context_->listenerScope(); }
  uint64_t listenerTag() const override { return listener_tag_; }
  const std::string& name() const override { return name_; }
  Network::UdpListenerConfigOptRef udpListenerConfig() override {
    return udp_listener_config_ != nullptr ? *udp_listener_config_
                                           : Network::UdpListenerConfigOptRef();
  }
  Network::ConnectionBalancer& connectionBalancer() override { return *connection_balancer_; }
  ResourceLimit& openConnections() override { return *open_connections_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return tcp_backlog_size_; }
  Init::Manager& initManager() override;
  envoy::config::core::v3::TrafficDirection direction() const override {
    return config().traffic_direction();
  }

  void ensureSocketOptions() {
    if (!listen_socket_options_) {
      listen_socket_options_ =
          std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    }
  }

  // Network::FilterChainFactory
  bool createNetworkFilterChain(Network::Connection& connection,
                                const std::vector<Network::FilterFactoryCb>& factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& manager) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;

  SystemTime last_updated_;

private:
  struct UdpListenerConfigImpl : public Network::UdpListenerConfig {
    UdpListenerConfigImpl(const envoy::config::listener::v3::UdpListenerConfig config)
        : config_(config) {}

    // Network::UdpListenerConfig
    Network::ActiveUdpListenerFactory& listenerFactory() override { return *listener_factory_; }
    Network::UdpPacketWriterFactory& packetWriterFactory() override { return *writer_factory_; }
    Network::UdpListenerWorkerRouter& listenerWorkerRouter() override {
      return *listener_worker_router_;
    }
    const envoy::config::listener::v3::UdpListenerConfig& config() override { return config_; }

    const envoy::config::listener::v3::UdpListenerConfig config_;
    Network::ActiveUdpListenerFactoryPtr listener_factory_;
    Network::UdpPacketWriterFactoryPtr writer_factory_;
    Network::UdpListenerWorkerRouterPtr listener_worker_router_;
  };

  /**
   * Create a new listener from an existing listener and the new config message if the in place
   * filter chain update is decided. Should be called only by newListenerWithFilterChain().
   */
  ListenerImpl(ListenerImpl& origin, const envoy::config::listener::v3::Listener& config,
               const std::string& version_info, ListenerManagerImpl& parent,
               const std::string& name, bool added_via_api, bool workers_started, uint64_t hash,
               uint32_t concurrency);
  // Helpers for constructor.
  void buildAccessLog();
  void buildUdpListenerFactory(Network::Socket::Type socket_type, uint32_t concurrency);
  void buildListenSocketOptions(Network::Socket::Type socket_type);
  void createListenerFilterFactories(Network::Socket::Type socket_type);
  void validateFilterChains(Network::Socket::Type socket_type);
  void buildFilterChains();
  void buildSocketOptions();
  void buildOriginalDstListenerFilter();
  void buildProxyProtocolListenerFilter();

  void addListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) {
    ensureSocketOptions();
    Network::Socket::appendOptions(listen_socket_options_, options);
  }

  ListenerManagerImpl& parent_;
  Network::Address::InstanceConstSharedPtr address_;

  Network::ListenSocketFactoryPtr socket_factory_;
  const bool bind_to_port_;
  const bool hand_off_restored_destination_connections_;
  const uint32_t per_connection_buffer_limit_bytes_;
  const uint64_t listener_tag_;
  const std::string name_;
  const bool added_via_api_;
  const bool workers_started_;
  const uint64_t hash_;
  const uint32_t tcp_backlog_size_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  // A target is added to Server's InitManager if workers_started_ is false.
  Init::TargetImpl listener_init_target_;
  // This init manager is populated with targets from the filter chain factories, namely
  // RdsRouteConfigSubscription::init_target_, so the listener can wait for route configs.
  std::unique_ptr<Init::Manager> dynamic_init_manager_;

  std::vector<Network::ListenerFilterFactoryCb> listener_filter_factories_;
  std::vector<Network::UdpListenerFilterFactoryCb> udp_listener_filter_factories_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  DrainManagerPtr local_drain_manager_;
  const envoy::config::listener::v3::Listener config_;
  const std::string version_info_;
  Network::Socket::OptionsSharedPtr listen_socket_options_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::unique_ptr<UdpListenerConfigImpl> udp_listener_config_;
  Network::ConnectionBalancerSharedPtr connection_balancer_;
  std::shared_ptr<PerListenerFactoryContextImpl> listener_factory_context_;
  FilterChainManagerImpl filter_chain_manager_;
  const bool reuse_port_;

  // Per-listener connection limits are only specified via runtime.
  //
  // TODO (tonya11en): Move this functionality into the overload manager.
  const std::string cx_limit_runtime_key_;
  std::shared_ptr<BasicResourceLimitImpl> open_connections_;

  // This init watcher, if workers_started_ is false, notifies the "parent" listener manager when
  // listener initialization is complete.
  // Important: local_init_watcher_ must be the last field in the class to avoid unexpected watcher
  // callback during the destroy of ListenerImpl.
  Init::WatcherImpl local_init_watcher_;

  Quic::QuicStatNames& quic_stat_names_;

  // to access ListenerManagerImpl::factory_.
  friend class ListenerFilterChainFactoryBuilder;
};

} // namespace Server
} // namespace Envoy
