#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"
#include "common/init/manager_impl.h"

#include "server/filter_chain_manager_impl.h"

namespace Envoy {
namespace Server {

class ListenerManagerImpl;

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
  Network::ConnectionBalancer& connectionBalancer() override { return *connection_balancer_; }

  // Server::Configuration::ListenerFactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  bool healthCheckFailed() override;
  Tracing::HttpTracer& httpTracer() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::RandomGenerator& random() override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::Instance& threadLocal() override;
  Admin& admin() override;
  const envoy::api::v2::core::Metadata& listenerMetadata() const override;
  envoy::api::v2::core::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  const Network::ListenerConfig& listenerConfig() const override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  OptProcessContextRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;

  void ensureSocketOptions() {
    if (!listen_socket_options_) {
      listen_socket_options_ =
          std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    }
  }
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
  Network::ConnectionBalancerPtr connection_balancer_;

  // to access ListenerManagerImpl::factory_.
  friend class ListenerFilterChainFactoryBuilder;
};

} // namespace Server
} // namespace Envoy