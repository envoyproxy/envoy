#pragma once

#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/common/basic_resource_impl.h"
#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/listener_manager/listener_info_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/server/factory_context_impl.h"
#include "source/server/transport_socket_config_impl.h"

namespace Envoy {
namespace Server {

/**
 * All missing listener config stats. @see stats_macros.h
 */
#define ALL_MISSING_LISTENER_CONFIG_STATS(COUNTER)                                                 \
  COUNTER(extension_config_missing)                                                                \
  COUNTER(network_extension_config_missing)

/**
 * Struct definition for all missing listener config stats. @see stats_macros.h
 */
struct MissingListenerConfigStats {
  ALL_MISSING_LISTENER_CONFIG_STATS(GENERATE_COUNTER_STRUCT)
};

class ListenerMessageUtil {
public:
  /**
   * @return true if listener message lhs and rhs have the same socket options.
   */
  static bool socketOptionsEqual(const envoy::config::listener::v3::Listener& lhs,
                                 const envoy::config::listener::v3::Listener& rhs);

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
  static absl::StatusOr<std::unique_ptr<ListenSocketFactoryImpl>>
  create(ListenerComponentFactory& factory, Network::Address::InstanceConstSharedPtr address,
         Network::Socket::Type socket_type, const Network::Socket::OptionsSharedPtr& options,
         const std::string& listener_name, uint32_t tcp_backlog_size,
         ListenerComponentFactory::BindType bind_type,
         const Network::SocketCreationOptions& creation_options, uint32_t num_sockets);

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
  absl::Status doFinalPreWorkerInit() override;

private:
  ListenSocketFactoryImpl(ListenerComponentFactory& factory,
                          Network::Address::InstanceConstSharedPtr address,
                          Network::Socket::Type socket_type,
                          const Network::Socket::OptionsSharedPtr& options,
                          const std::string& listener_name, uint32_t tcp_backlog_size,
                          ListenerComponentFactory::BindType bind_type,
                          const Network::SocketCreationOptions& creation_options,
                          uint32_t num_sockets, absl::Status& creation_status);

  ListenSocketFactoryImpl(const ListenSocketFactoryImpl& factory_to_clone);

  absl::StatusOr<Network::SocketSharedPtr>
  createListenSocketAndApplyOptions(ListenerComponentFactory& factory,
                                    Network::Socket::Type socket_type, uint32_t worker_index);

  ListenerComponentFactory& factory_;
  // Initially, its port number might be 0. Once a socket is created, its port
  // will be set to the binding port.
  Network::Address::InstanceConstSharedPtr local_address_;
  const Network::Socket::Type socket_type_;
  const Network::Socket::OptionsSharedPtr options_;
  const std::string listener_name_;
  const uint32_t tcp_backlog_size_;
  ListenerComponentFactory::BindType bind_type_;
  const Network::SocketCreationOptions socket_creation_options_;
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

class ListenerImpl;

/**
 * The common functionality shared by PerListenerFilterFactoryContexts and
 * PerFilterChainFactoryFactoryContexts.
 */
class ListenerFactoryContextBaseImpl final : public Server::FactoryContextImplBase,
                                             public Network::DrainDecision {
public:
  ListenerFactoryContextBaseImpl(Envoy::Server::Instance& server,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 const envoy::config::listener::v3::Listener& config,
                                 Server::DrainManagerPtr drain_manager);

  Init::Manager& initManager() override;
  Network::DrainDecision& drainDecision() override;

  // DrainDecision
  bool drainClose() const override {
    return drain_manager_->drainClose() || server_.drainManager().drainClose();
  }
  Common::CallbackHandlePtr addOnDrainCloseCb(DrainCloseCb) const override {
    IS_ENVOY_BUG("Unexpected function call");
    return nullptr;
  }
  Server::DrainManager& drainManager();
  friend class ListenerImpl;

private:
  const Server::DrainManagerPtr drain_manager_;
};

// TODO(lambdai): Strip the interface since ListenerFactoryContext only need to support
// ListenerFilterChain creation. e.g, Is listenerMetaData() required? Is it required only at
// listener update or during the lifetime of listener?
class PerListenerFactoryContextImpl : public Configuration::ListenerFactoryContext {
public:
  PerListenerFactoryContextImpl(Envoy::Server::Instance& server,
                                ProtobufMessage::ValidationVisitor& validation_visitor,
                                const envoy::config::listener::v3::Listener& config_message,
                                ListenerImpl& listener_impl, DrainManagerPtr drain_manager);

  PerListenerFactoryContextImpl(
      std::shared_ptr<ListenerFactoryContextBaseImpl> listener_factory_context_base,
      ListenerImpl& listener_impl)
      : listener_factory_context_base_(listener_factory_context_base),
        listener_impl_(listener_impl) {}

  // FactoryContext
  Network::DrainDecision& drainDecision() override;
  Init::Manager& initManager() override;
  Stats::Scope& scope() override;
  const Network::ListenerInfo& listenerInfo() const override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override;
  Configuration::ServerFactoryContext& serverFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;

  Stats::Scope& listenerScope() override;

  ListenerFactoryContextBaseImpl& parentFactoryContext() { return *listener_factory_context_base_; }
  friend class ListenerImpl;

private:
  std::shared_ptr<ListenerFactoryContextBaseImpl> listener_factory_context_base_;
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
  static absl::StatusOr<std::unique_ptr<ListenerImpl>>
  create(const envoy::config::listener::v3::Listener& config, const std::string& version_info,
         ListenerManagerImpl& parent, const std::string& name, bool added_via_api,
         bool workers_started, uint64_t hash);
  ~ListenerImpl() override;

  // TODO(lambdai): Explore using the same ListenerImpl object to execute in place filter chain
  // update.
  /**
   * Execute in place filter chain update. The filter chain update is less expensive than full
   * listener update because connections may not need to be drained.
   */
  absl::StatusOr<std::unique_ptr<ListenerImpl>>
  newListenerWithFilterChain(const envoy::config::listener::v3::Listener& config,
                             bool workers_started, uint64_t hash);
  /**
   * Determine if in place filter chain update could be executed at this moment.
   */
  bool supportUpdateFilterChain(const envoy::config::listener::v3::Listener& new_config,
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

  const std::vector<Network::Address::InstanceConstSharedPtr>& addresses() const {
    return addresses_;
  }
  const envoy::config::listener::v3::Listener& config() const { return config_; }
  const std::vector<Network::ListenSocketFactoryPtr>& getSocketFactories() const {
    return socket_factories_;
  }
  void debugLog(const std::string& message);
  void initialize();
  DrainManager& localDrainManager() const {
    return listener_factory_context_->listener_factory_context_base_->drainManager();
  }
  absl::Status addSocketFactory(Network::ListenSocketFactoryPtr&& socket_factory);
  void setSocketAndOptions(const Network::SocketSharedPtr& socket);
  const Network::Socket::OptionsSharedPtr& listenSocketOptions(uint32_t address_index) {
    ASSERT(listen_socket_options_list_.size() > address_index);
    return listen_socket_options_list_[address_index];
  }
  const std::string& versionInfo() const { return version_info_; }
  bool reusePort() const { return reuse_port_; }
  static bool getReusePortOrDefault(Server::Instance& server,
                                    const envoy::config::listener::v3::Listener& config,
                                    Network::Socket::Type socket_type);

  // Compare whether two listeners have different socket options.
  bool socketOptionsEqual(const ListenerImpl& other) const;
  // Check whether a new listener can share sockets with this listener.
  bool hasCompatibleAddress(const ListenerImpl& other) const;
  // Check whether a new listener has duplicated listening address this listener.
  bool hasDuplicatedAddress(const ListenerImpl& other) const;

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return *filter_chain_manager_; }
  Network::FilterChainFactory& filterChainFactory() override { return *this; }
  std::vector<Network::ListenSocketFactoryPtr>& listenSocketFactories() override {
    return socket_factories_;
  }
  bool bindToPort() const override { return bind_to_port_; }
  bool mptcpEnabled() { return mptcp_enabled_; }
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
  Network::InternalListenerConfigOptRef internalListenerConfig() override {
    return internal_listener_config_ != nullptr ? *internal_listener_config_
                                                : Network::InternalListenerConfigOptRef();
  }
  Network::ConnectionBalancer&
  connectionBalancer(const Network::Address::Instance& address) override {
    auto balancer = connection_balancers_.find(address.asString());
    ASSERT(balancer != connection_balancers_.end());
    return *balancer->second;
  }
  ResourceLimit& openConnections() override { return *open_connections_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return access_logs_;
  }
  uint32_t tcpBacklogSize() const override { return tcp_backlog_size_; }
  uint32_t maxConnectionsToAcceptPerSocketEvent() const override {
    return max_connections_to_accept_per_socket_event_;
  }
  Init::Manager& initManager() override;
  bool ignoreGlobalConnLimit() const override { return ignore_global_conn_limit_; }
  bool shouldBypassOverloadManager() const override { return bypass_overload_manager_; }
  const Network::ListenerInfoConstSharedPtr& listenerInfo() const override {
    ASSERT(listener_factory_context_ != nullptr);
    return listener_factory_context_->listener_factory_context_base_->listener_info_;
  }

  void ensureSocketOptions(Network::Socket::OptionsSharedPtr& options) {
    if (options == nullptr) {
      options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    }
  }

  absl::Status cloneSocketFactoryFrom(const ListenerImpl& other);
  void closeAllSockets();

  Network::Socket::Type socketType() const { return socket_type_; }

  // Network::FilterChainFactory
  bool createNetworkFilterChain(Network::Connection& connection,
                                const Filter::NetworkFilterFactoriesList& factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& manager) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;
  bool createQuicListenerFilterChain(Network::QuicListenerFilterManager& manager) override;

  SystemTime last_updated_;

private:
  ListenerImpl(const envoy::config::listener::v3::Listener& config, const std::string& version_info,
               ListenerManagerImpl& parent, const std::string& name, bool added_via_api,
               bool workers_started, uint64_t hash, absl::Status& creation_status);
  struct UdpListenerConfigImpl : public Network::UdpListenerConfig {
    UdpListenerConfigImpl(const envoy::config::listener::v3::UdpListenerConfig config)
        : config_(config) {}

    // Network::UdpListenerConfig
    Network::ActiveUdpListenerFactory& listenerFactory() override { return *listener_factory_; }
    Network::UdpPacketWriterFactory& packetWriterFactory() override { return *writer_factory_; }
    Network::UdpListenerWorkerRouter&
    listenerWorkerRouter(const Network::Address::Instance& address) override {
      auto iter = listener_worker_routers_.find(address.asString());
      ASSERT(iter != listener_worker_routers_.end());
      return *iter->second;
    }
    const envoy::config::listener::v3::UdpListenerConfig& config() override { return config_; }

    const envoy::config::listener::v3::UdpListenerConfig config_;
    Network::ActiveUdpListenerFactoryPtr listener_factory_;
    Network::UdpPacketWriterFactoryPtr writer_factory_;
    absl::flat_hash_map<std::string, Network::UdpListenerWorkerRouterPtr> listener_worker_routers_;
  };

  class InternalListenerConfigImpl : public Network::InternalListenerConfig {
  public:
    InternalListenerConfigImpl(Network::InternalListenerRegistry& internal_listener_registry)
        : internal_listener_registry_(internal_listener_registry) {}

    Network::InternalListenerRegistry& internalListenerRegistry() override {
      return internal_listener_registry_;
    }

  private:
    Network::InternalListenerRegistry& internal_listener_registry_;
  };

  /**
   * Create a new listener from an existing listener and the new config message if the in place
   * filter chain update is decided. Should be called only by newListenerWithFilterChain().
   */
  ListenerImpl(ListenerImpl& origin, const envoy::config::listener::v3::Listener& config,
               const std::string& version_info, ListenerManagerImpl& parent,
               const std::string& name, bool added_via_api, bool workers_started, uint64_t hash,
               absl::Status& creation_status);
  // Helpers for constructor.
  void buildAccessLog(const envoy::config::listener::v3::Listener& config);
  absl::Status buildInternalListener(const envoy::config::listener::v3::Listener& config);
  absl::Status validateConfig();
  bool buildUdpListenerWorkerRouter(const Network::Address::Instance& address,
                                    uint32_t concurrency);
  absl::Status buildUdpListenerFactory(const envoy::config::listener::v3::Listener& config,
                                       uint32_t concurrency);
  void buildListenSocketOptions(const envoy::config::listener::v3::Listener& config,
                                std::vector<std::reference_wrapper<const Protobuf::RepeatedPtrField<
                                    envoy::config::core::v3::SocketOption>>>& address_opts_list);
  absl::Status createListenerFilterFactories(const envoy::config::listener::v3::Listener& config);
  absl::Status validateFilterChains(const envoy::config::listener::v3::Listener& config);
  absl::Status buildFilterChains(const envoy::config::listener::v3::Listener& config);
  absl::Status buildConnectionBalancer(const envoy::config::listener::v3::Listener& config,
                                       const Network::Address::Instance& address);
  void buildSocketOptions(const envoy::config::listener::v3::Listener& config);
  void buildOriginalDstListenerFilter(const envoy::config::listener::v3::Listener& config);
  void buildProxyProtocolListenerFilter(const envoy::config::listener::v3::Listener& config);
  absl::Status checkIpv4CompatAddress(const Network::Address::InstanceConstSharedPtr& address,
                                      const envoy::config::core::v3::Address& proto_address);

  void addListenSocketOptions(Network::Socket::OptionsSharedPtr& options,
                              const Network::Socket::OptionsSharedPtr& append_options) {
    ensureSocketOptions(options);
    Network::Socket::appendOptions(options, append_options);
  }

  ListenerManagerImpl& parent_;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses_;
  const Network::Socket::Type socket_type_;

  std::vector<Network::ListenSocketFactoryPtr> socket_factories_;
  const bool bind_to_port_;
  const bool mptcp_enabled_;
  const bool hand_off_restored_destination_connections_;
  const uint32_t per_connection_buffer_limit_bytes_;
  const uint64_t listener_tag_;
  const std::string name_;
  const bool added_via_api_;
  const bool workers_started_;
  const uint64_t hash_;
  const uint32_t tcp_backlog_size_;
  const uint32_t max_connections_to_accept_per_socket_event_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const bool ignore_global_conn_limit_;
  const bool bypass_overload_manager_;

  // A target is added to Server's InitManager if workers_started_ is false.
  Init::TargetImpl listener_init_target_;
  // This init manager is populated with targets from the filter chain factories, namely
  // RdsRouteConfigSubscription::init_target_, so the listener can wait for route configs.
  std::unique_ptr<Init::Manager> dynamic_init_manager_;

  Filter::ListenerFilterFactoriesList listener_filter_factories_;
  std::vector<Network::UdpListenerFilterFactoryCb> udp_listener_filter_factories_;
  Filter::QuicListenerFilterFactoriesList quic_listener_filter_factories_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  const envoy::config::listener::v3::Listener config_;
  const std::string version_info_;
  // Using std::vector instead of hash map for supporting multiple zero port addresses.
  std::vector<Network::Socket::OptionsSharedPtr> listen_socket_options_list_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::shared_ptr<UdpListenerConfigImpl> udp_listener_config_;
  std::unique_ptr<Network::InternalListenerConfig> internal_listener_config_;
  // The key is the address string, the value is the address specific connection balancer.
  // TODO (soulxu): Add hash support for address, then needn't a string address as key anymore.
  absl::flat_hash_map<std::string, Network::ConnectionBalancerSharedPtr> connection_balancers_;
  std::shared_ptr<PerListenerFactoryContextImpl> listener_factory_context_;
  std::unique_ptr<FilterChainManagerImpl> filter_chain_manager_;
  const bool reuse_port_;

  // Per-listener connection limits are only specified via runtime.
  //
  // TODO (tonya11en): Move this functionality into the overload manager.
  const std::string cx_limit_runtime_key_;
  std::shared_ptr<BasicResourceLimitImpl> open_connections_;

  // This init watcher, if workers_started_ is false, notifies the "parent" listener manager when
  // listener initialization is complete.
  // Important: local_init_watcher_ must be the last field in the class to avoid unexpected
  // watcher callback during the destroy of ListenerImpl.
  Init::WatcherImpl local_init_watcher_;
  std::shared_ptr<Server::Configuration::TransportSocketFactoryContextImpl>
      transport_factory_context_;

  Quic::QuicStatNames& quic_stat_names_;
  MissingListenerConfigStats missing_listener_config_stats_;

  // to access ListenerManagerImpl::factory_.
  friend class ListenerFilterChainFactoryBuilder;
};

} // namespace Server
} // namespace Envoy
