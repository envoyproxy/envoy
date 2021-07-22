#pragma once

#include <iostream>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/tracing/http_tracer.h"

#include "source/common/access_log/access_log_manager_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/random_generator.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/rds_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/server/admin/admin.h"
#include "source/server/config_validation/admin.h"
#include "source/server/config_validation/api.h"
#include "source/server/config_validation/cluster_manager.h"
#include "source/server/config_validation/dns.h"
#include "source/server/listener_manager_impl.h"
#include "source/server/server.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

/**
 * validateConfig() takes over from main() for a config-validation run of Envoy. It returns true if
 * the config is valid, false if invalid.
 */
bool validateConfig(const Options& options,
                    const Network::Address::InstanceConstSharedPtr& local_address,
                    ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                    Filesystem::Instance& file_system);

/**
 * ValidationInstance does the bulk of the work for config-validation runs of Envoy. It implements
 * Server::Instance, but some functionality not needed until serving time, such as updating
 * health-check status, is not implemented. Everything else is written in terms of other
 * validation-specific interface implementations, with the end result that we can load and
 * initialize a configuration, skipping any steps that affect the outside world (such as
 * hot-restarting or connecting to upstream clusters) but otherwise exercising the entire startup
 * flow.
 *
 * If we finish initialization, and reach the point where an ordinary Envoy run would begin serving
 * requests, the validation is considered successful.
 */
class ValidationInstance final : Logger::Loggable<Logger::Id::main>,
                                 public Instance,
                                 public ListenerComponentFactory,
                                 public ServerLifecycleNotifier,
                                 public WorkerFactory {
public:
  ValidationInstance(const Options& options, Event::TimeSystem& time_system,
                     const Network::Address::InstanceConstSharedPtr& local_address,
                     Stats::IsolatedStoreImpl& store, Thread::BasicLockable& access_log_lock,
                     ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                     Filesystem::Instance& file_system);

  // Server::Instance
  Admin& admin() override { return *admin_; }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override { return *config_.clusterManager(); }
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override {
    return dispatcher().createDnsResolver({}, envoy::config::core::v3::DnsResolverOptions());
  }
  void drainListeners() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  DrainManager& drainManager() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  void failHealthcheck(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  HotRestart& hotRestart() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Init::Manager& initManager() override { return init_manager_; }
  ServerLifecycleNotifier& lifecycleNotifier() override { return *this; }
  ListenerManager& listenerManager() override { return *listener_manager_; }
  Secret::SecretManager& secretManager() override { return *secret_manager_; }
  Runtime::Loader& runtime() override { return Runtime::LoaderSingleton::get(); }
  void shutdown() override;
  bool isShutdown() override { return false; }
  void shutdownAdmin() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  OverloadManager& overloadManager() override { return *overload_manager_; }
  bool healthCheckFailed() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  const Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  time_t startTimeFirstEpoch() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Stats::Store& stats() override { return stats_store_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  Router::Context& routerContext() override { return router_context_; }
  ProcessContextOptRef processContext() override { return absl::nullopt; }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  LocalInfo::LocalInfo& localInfo() const override { return *local_info_; }
  TimeSource& timeSource() override { return api_->timeSource(); }
  Envoy::MutexTracer* mutexTracer() override { return mutex_tracer_; }
  void flushStats() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }
  bool enableReusePortDefault() override { return true; }

  Configuration::StatsConfig& statsConfig() override { return config_.statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Configuration::ServerFactoryContext& serverFactoryContext() override { return server_contexts_; }
  Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() override {
    return server_contexts_;
  }
  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }

  // Server::ListenerComponentFactory
  LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config,
                         const xds::core::v3::ResourceLocator* lds_resources_locator) override {
    return std::make_unique<LdsApiImpl>(lds_config, lds_resources_locator, clusterManager(),
                                        initManager(), stats(), listenerManager(),
                                        messageValidationContext().dynamicValidationVisitor());
  }
  std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
      Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) override {
    return ProdListenerComponentFactory::createNetworkFilterFactoryList_(
        filters, filter_chain_factory_context);
  }
  std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createListenerFilterFactoryList_(filters, context);
  }
  std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(filters, context);
  }
  Network::SocketSharedPtr createListenSocket(Network::Address::InstanceConstSharedPtr,
                                              Network::Socket::Type,
                                              const Network::Socket::OptionsSharedPtr&,
                                              ListenerComponentFactory::BindType,
                                              uint32_t) override {
    // Returned sockets are not currently used so we can return nothing here safely vs. a
    // validation mock.
    // TODO(mattklein123): The fact that this returns nullptr makes the production code more
    // convoluted than it needs to be. Fix this to return a mock in a follow up.
    return nullptr;
  }
  DrainManagerPtr createDrainManager(envoy::config::listener::v3::Listener::DrainType) override {
    return nullptr;
  }
  uint64_t nextListenerTag() override { return 0; }

  // Server::WorkerFactory
  WorkerPtr createWorker(uint32_t, OverloadManager&, const std::string&) override {
    // Returned workers are not currently used so we can return nothing here safely vs. a
    // validation mock.
    return nullptr;
  }

  // ServerLifecycleNotifier
  ServerLifecycleNotifier::HandlePtr registerCallback(Stage, StageCallback) override {
    return nullptr;
  }
  ServerLifecycleNotifier::HandlePtr registerCallback(Stage, StageCallbackWithCompletion) override {
    return nullptr;
  }

private:
  void initialize(const Options& options,
                  const Network::Address::InstanceConstSharedPtr& local_address,
                  ComponentFactory& component_factory);

  // init_manager_ must come before any member that participates in initialization, and destructed
  // only after referencing members are gone, since initialization continuation can potentially
  // occur at any point during member lifetime.
  Init::ManagerImpl init_manager_{"Validation server"};
  Init::WatcherImpl init_watcher_{"(no-op)", []() {}};
  // secret_manager_ must come before listener_manager_, config_ and dispatcher_, and destructed
  // only after these members can no longer reference it, since:
  // - There may be active filter chains referencing it in listener_manager_.
  // - There may be active clusters referencing it in config_.cluster_manager_.
  // - There may be active connections referencing it.
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  const Options& options_;
  ProtobufMessage::ProdValidationContextImpl validation_context_;
  Stats::IsolatedStoreImpl& stats_store_;
  ThreadLocal::InstanceImpl thread_local_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<Server::ValidationAdmin> admin_;
  Singleton::ManagerPtr singleton_manager_;
  std::unique_ptr<Runtime::ScopedLoaderSingleton> runtime_singleton_;
  Random::RandomGeneratorImpl random_generator_;
  std::unique_ptr<Ssl::ContextManager> ssl_context_manager_;
  Configuration::MainImpl config_;
  LocalInfo::LocalInfoPtr local_info_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::unique_ptr<Upstream::ValidationClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<ListenerManagerImpl> listener_manager_;
  std::unique_ptr<OverloadManager> overload_manager_;
  MutexTracer* mutex_tracer_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  Event::TimeSystem& time_system_;
  ServerFactoryContextImpl server_contexts_;
  Quic::QuicStatNames quic_stat_names_;
};

} // namespace Server
} // namespace Envoy
