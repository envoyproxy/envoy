#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/server/worker.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/init/manager_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "server/filter_chain_manager_impl.h"
#include "server/lds_api.h"

namespace Envoy {
namespace Server {

namespace Configuration {
class TransportSocketFactoryContextImpl;
}

class ListenerFilterChainFactoryBuilder;

/**
 * Prod implementation of ListenerComponentFactory that creates real sockets and attempts to fetch
 * sockets from the parent process via the hot restarter. The filter factory list is created from
 * statically registered filters.
 */
class ProdListenerComponentFactory : public ListenerComponentFactory,
                                     Logger::Loggable<Logger::Id::config> {
public:
  ProdListenerComponentFactory(Instance& server) : server_(server) {}

  /**
   * Static worker for createNetworkFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context);
  /**
   * Static worker for createListenerFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context);

  /**
   * Static worker for createUdpListenerFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context);

  // Server::ListenerComponentFactory
  LdsApiPtr createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    return std::make_unique<LdsApiImpl>(
        lds_config, server_.clusterManager(), server_.initManager(), server_.stats(),
        server_.listenerManager(), server_.messageValidationContext().dynamicValidationVisitor());
  }
  std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context) override {
    return createNetworkFilterFactoryList_(filters, context);
  }
  std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return createListenerFilterFactoryList_(filters, context);
  }
  std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return createUdpListenerFilterFactoryList_(filters, context);
  }
  Network::SocketSharedPtr createListenSocket(Network::Address::InstanceConstSharedPtr address,
                                              Network::Address::SocketType socket_type,
                                              const Network::Socket::OptionsSharedPtr& options,
                                              bool bind_to_port) override;
  DrainManagerPtr createDrainManager(envoy::api::v2::Listener::DrainType drain_type) override;
  uint64_t nextListenerTag() override { return next_listener_tag_++; }

private:
  Instance& server_;
  uint64_t next_listener_tag_{1};
};

class ListenerImpl;
using ListenerImplPtr = std::unique_ptr<ListenerImpl>;

/**
 * All listener manager stats. @see stats_macros.h
 */
#define ALL_LISTENER_MANAGER_STATS(COUNTER, GAUGE)                                                 \
  COUNTER(listener_added)                                                                          \
  COUNTER(listener_create_failure)                                                                 \
  COUNTER(listener_create_success)                                                                 \
  COUNTER(listener_modified)                                                                       \
  COUNTER(listener_removed)                                                                        \
  GAUGE(total_listeners_active, NeverImport)                                                       \
  GAUGE(total_listeners_draining, NeverImport)                                                     \
  GAUGE(total_listeners_warming, NeverImport)

/**
 * Struct definition for all listener manager stats. @see stats_macros.h
 */
struct ListenerManagerStats {
  ALL_LISTENER_MANAGER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class ServerFactoryContextImpl : public Configuration::FactoryContext {
public:
  explicit ServerFactoryContextImpl(Instance& server)
      : server_(server), server_scope_(server_.stats().createScope("")) {}

  // Server::Configuration::ListenerFactoryContext
  AccessLog::AccessLogManager& accessLogManager() override { return server_.accessLogManager(); }
  Upstream::ClusterManager& clusterManager() override { return server_.clusterManager(); }
  Event::Dispatcher& dispatcher() override { return server_.dispatcher(); }
  Grpc::Context& grpcContext() override { return server_.grpcContext(); }
  bool healthCheckFailed() override { return server_.healthCheckFailed(); }
  Tracing::HttpTracer& httpTracer() override { return httpContext().tracer(); }
  Http::Context& httpContext() override { return server_.httpContext(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_.localInfo(); }
  Envoy::Runtime::RandomGenerator& random() override { return server_.random(); }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Singleton::Manager& singletonManager() override { return server_.singletonManager(); }
  OverloadManager& overloadManager() override { return server_.overloadManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }
  Admin& admin() override { return server_.admin(); }
  TimeSource& timeSource() override { return api().timeSource(); }
  Api::Api& api() override { return server_.api(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return server_.lifecycleNotifier(); }
  ProcessContext& processContext() override { return server_.processContext(); }

  // ugly
  virtual Stats::Scope& scope() override { return *server_scope_; }
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  virtual envoy::api::v2::core::TrafficDirection direction() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  virtual const Network::DrainDecision& drainDecision() override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  virtual Init::Manager& initManager() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  virtual Stats::Scope& listenerScope() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  virtual const envoy::api::v2::core::Metadata& listenerMetadata() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  virtual Configuration::FactoryContext* getServerContext() override { return this; }

private:
  Instance& server_;
  Stats::ScopePtr server_scope_;
};

/**
 * Implementation of ListenerManager.
 */
class ListenerManagerImpl : public ListenerManager, Logger::Loggable<Logger::Id::config> {
public:
  ListenerManagerImpl(Instance& server, ListenerComponentFactory& listener_factory,
                      WorkerFactory& worker_factory, bool enable_dispatcher_stats);

  void onListenerWarmed(ListenerImpl& listener);

  // Server::ListenerManager
  bool addOrUpdateListener(const envoy::api::v2::Listener& config, const std::string& version_info,
                           bool added_via_api) override;
  void createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    ASSERT(lds_api_ == nullptr);
    lds_api_ = factory_.createLdsApi(lds_config);
  }
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners() override;
  uint64_t numConnections() override;
  bool removeListener(const std::string& listener_name) override;
  void startWorkers(GuardDog& guard_dog) override;
  void stopListeners() override;
  void stopWorkers() override;
  Http::Context& httpContext() { return server_.httpContext(); }

  Instance& server_;
  ServerFactoryContextImpl server_factory_context_;
  ListenerComponentFactory& factory_;

private:
  using ListenerList = std::list<ListenerImplPtr>;

  struct DrainingListener {
    DrainingListener(ListenerImplPtr&& listener, uint64_t workers_pending_removal)
        : listener_(std::move(listener)), workers_pending_removal_(workers_pending_removal) {}

    ListenerImplPtr listener_;
    uint64_t workers_pending_removal_;
  };

  void addListenerToWorker(Worker& worker, ListenerImpl& listener);
  ProtobufTypes::MessagePtr dumpListenerConfigs();
  static ListenerManagerStats generateStats(Stats::Scope& scope);
  static bool hasListenerWithAddress(const ListenerList& list,
                                     const Network::Address::Instance& address);
  void updateWarmingActiveGauges() {
    // Using set() avoids a multiple modifiers problem during the multiple processes phase of hot
    // restart.
    stats_.total_listeners_warming_.set(warming_listeners_.size());
    stats_.total_listeners_active_.set(active_listeners_.size());
  }

  /**
   * Mark a listener for draining. The listener will no longer be considered active but will remain
   * present to allow connection draining.
   * @param listener supplies the listener to drain.
   */
  void drainListener(ListenerImplPtr&& listener);

  /**
   * Get a listener by name. This routine is used because listeners have inherent order in static
   * configuration and especially for tests. Thus, we can't use a map.
   * @param listeners supplies the listener list to look in.
   * @param name supplies the name to search for.
   */
  ListenerList::iterator getListenerByName(ListenerList& listeners, const std::string& name);

  // Active listeners are listeners that are currently accepting new connections on the workers.
  ListenerList active_listeners_;
  // Warming listeners are listeners that may need further initialization via the listener's init
  // manager. For example, RDS, or in the future KDS. Once a listener is done warming it will
  // be transitioned to active.
  ListenerList warming_listeners_;
  // Draining listeners are listeners that are in the process of being drained and removed. They
  // go through two phases where first the workers stop accepting new connections and existing
  // connections are drained. Then after that time period the listener is removed from all workers
  // and any remaining connections are closed.
  std::list<DrainingListener> draining_listeners_;
  std::list<WorkerPtr> workers_;
  bool workers_started_{};
  Stats::ScopePtr scope_;
  ListenerManagerStats stats_;
  ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  LdsApiPtr lds_api_;
  const bool enable_dispatcher_stats_{};
};

// TODO(mattklein123): Consider getting rid of pre-worker start and post-worker start code by
//                     initializing all listeners after workers are started.

/**
 * Maps proto config to runtime config for a listener with a network filter chain.
 */
class ListenerImpl : public Network::ListenerConfig,
                     public Configuration::ListenerFactoryContext,
                     public Network::DrainDecision,
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
   * @param validation_visitor message validation visitor instance.
   */
  ListenerImpl(const envoy::api::v2::Listener& config, const std::string& version_info,
               ListenerManagerImpl& parent, const std::string& name, bool added_via_api,
               bool workers_started, uint64_t hash,
               ProtobufMessage::ValidationVisitor& validation_visitor);
  ~ListenerImpl() override;

  /**
   * Helper functions to determine whether a listener is blocked for update or remove.
   */
  bool blockUpdate(uint64_t new_hash) { return new_hash == hash_ || !added_via_api_; }
  bool blockRemove() { return !added_via_api_; }

  /**
   * Called when a listener failed to be actually created on a worker.
   * @return TRUE if we have seen more than one worker failure.
   */
  bool onListenerCreateFailure() {
    bool ret = saw_listener_create_failure_;
    saw_listener_create_failure_ = true;
    return ret;
  }

  Network::Address::InstanceConstSharedPtr address() const { return address_; }
  Network::Address::SocketType socketType() const { return socket_type_; }
  const envoy::api::v2::Listener& config() { return config_; }
  const Network::SocketSharedPtr& getSocket() const { return socket_; }
  void debugLog(const std::string& message);
  void initialize();
  DrainManager& localDrainManager() const { return *local_drain_manager_; }
  void setSocket(const Network::SocketSharedPtr& socket);
  void setSocketAndOptions(const Network::SocketSharedPtr& socket);
  const Network::Socket::OptionsSharedPtr& listenSocketOptions() { return listen_socket_options_; }
  const std::string& versionInfo() { return version_info_; }

  // Network::ListenerConfig
  Network::FilterChainManager& filterChainManager() override { return filter_chain_manager_; }
  Network::FilterChainFactory& filterChainFactory() override { return *this; }
  Network::Socket& socket() override { return *socket_; }
  const Network::Socket& socket() const override { return *socket_; }
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
  Stats::Scope& listenerScope() override { return *listener_scope_; }
  uint64_t listenerTag() const override { return listener_tag_; }
  const std::string& name() const override { return name_; }
  const Network::ActiveUdpListenerFactory* udpListenerFactory() override {
    return udp_listener_factory_.get();
  }

  // Server::Configuration::ListenerFactoryContext
  AccessLog::AccessLogManager& accessLogManager() override {
    return server_factory_context_.accessLogManager();
  }
  Upstream::ClusterManager& clusterManager() override {
    return server_factory_context_.clusterManager();
  }
  Event::Dispatcher& dispatcher() override { return server_factory_context_.dispatcher(); }
  Grpc::Context& grpcContext() override { return server_factory_context_.grpcContext(); }
  bool healthCheckFailed() override { return server_factory_context_.healthCheckFailed(); }
  Tracing::HttpTracer& httpTracer() override { return httpContext().tracer(); }
  Http::Context& httpContext() override { return server_factory_context_.httpContext(); }
  const LocalInfo::LocalInfo& localInfo() const override {
    return server_factory_context_.localInfo();
  }
  Envoy::Runtime::RandomGenerator& random() override { return server_factory_context_.random(); }
  Envoy::Runtime::Loader& runtime() override { return server_factory_context_.runtime(); }
  Singleton::Manager& singletonManager() override {
    return server_factory_context_.singletonManager();
  }
  OverloadManager& overloadManager() override { return server_factory_context_.overloadManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_factory_context_.threadLocal(); }
  Admin& admin() override { return server_factory_context_.admin(); }
  TimeSource& timeSource() override { return api().timeSource(); }
  Api::Api& api() override { return server_factory_context_.api(); }
  ServerLifecycleNotifier& lifecycleNotifier() override {
    return server_factory_context_.lifecycleNotifier();
  }
  ProcessContext& processContext() override { return server_factory_context_.processContext(); }

  // Overrided Servers
  virtual Configuration::FactoryContext* getServerContext() override;
  Network::DrainDecision& drainDecision() override { return *this; }
  Stats::Scope& scope() override { return *global_scope_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Init::Manager& initManager() override;

  const envoy::api::v2::core::Metadata& listenerMetadata() const override {
    return config_.metadata();
  };
  envoy::api::v2::core::TrafficDirection direction() const override {
    return config_.traffic_direction();
  };
  void ensureSocketOptions() {
    if (!listen_socket_options_) {
      listen_socket_options_ =
          std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    }
  }
  const Network::ListenerConfig& listenerConfig() const override { return *this; }

  // Network::DrainDecision
  bool drainClose() const override;

  // Network::FilterChainFactory
  bool createNetworkFilterChain(Network::Connection& connection,
                                const std::vector<Network::FilterFactoryCb>& factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& manager) override;
  bool createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;

  SystemTime last_updated_;

private:
  void addListenSocketOption(const Network::Socket::OptionConstSharedPtr& option) {
    ensureSocketOptions();
    listen_socket_options_->emplace_back(std::move(option));
  }
  void addListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) {
    ensureSocketOptions();
    Network::Socket::appendOptions(listen_socket_options_, options);
  }

  ListenerManagerImpl& parent_;
  ServerFactoryContextImpl& server_factory_context_;

  Network::Address::InstanceConstSharedPtr address_;
  FilterChainManagerImpl filter_chain_manager_;

  Network::Address::SocketType socket_type_;
  Network::SocketSharedPtr socket_;
  Stats::ScopePtr global_scope_;   // Stats with global named scope, but needed for LDS cleanup.
  Stats::ScopePtr listener_scope_; // Stats with listener named scope.
  const bool bind_to_port_;
  const bool hand_off_restored_destination_connections_;
  const uint32_t per_connection_buffer_limit_bytes_;
  const uint64_t listener_tag_;
  const std::string name_;
  const bool added_via_api_;
  const bool workers_started_;
  const uint64_t hash_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  // This init manager is populated with targets from the filter chain factories, namely
  // RdsRouteConfigSubscription::init_target_, so the listener can wait for route configs.
  Init::ManagerImpl dynamic_init_manager_;

  // This init watcher, if available, notifies the "parent" listener manager when listener
  // initialization is complete. It may be reset to cancel interest.
  std::unique_ptr<Init::WatcherImpl> init_watcher_;
  std::vector<Network::ListenerFilterFactoryCb> listener_filter_factories_;
  std::vector<Network::UdpListenerFilterFactoryCb> udp_listener_filter_factories_;
  DrainManagerPtr local_drain_manager_;
  bool saw_listener_create_failure_{};
  const envoy::api::v2::Listener config_;
  const std::string version_info_;
  Network::Socket::OptionsSharedPtr listen_socket_options_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  Network::ActiveUdpListenerFactoryPtr udp_listener_factory_;
  // to access ListenerManagerImpl::factory_.
  friend class ListenerFilterChainFactoryBuilder;
};

class ListenerFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  ListenerFilterChainFactoryBuilder(
      ListenerImpl& listener, Configuration::TransportSocketFactoryContextImpl& factory_context);
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) const override;

private:
  ListenerImpl& parent_;
  Configuration::TransportSocketFactoryContextImpl& factory_context_;
};

} // namespace Server
} // namespace Envoy
