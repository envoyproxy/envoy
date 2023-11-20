#pragma once

#include <iostream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/tracing/tracer.h"

#include "source/common/access_log/access_log_manager_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/random_generator.h"
#include "source/common/grpc/common.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/rds_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/server/config_validation/admin.h"
#include "source/server/config_validation/api.h"
#include "source/server/config_validation/cluster_manager.h"
#include "source/server/config_validation/dns.h"
#include "source/server/hot_restart_nop_impl.h"
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
                    Filesystem::Instance& file_system,
                    const ProcessContextOptRef& process_context = absl::nullopt);

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
                                 public ServerLifecycleNotifier,
                                 public WorkerFactory {
public:
  ValidationInstance(const Options& options, Event::TimeSystem& time_system,
                     const Network::Address::InstanceConstSharedPtr& local_address,
                     Stats::IsolatedStoreImpl& store, Thread::BasicLockable& access_log_lock,
                     ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                     Filesystem::Instance& file_system,
                     const ProcessContextOptRef& process_context = absl::nullopt);

  // Server::Instance
  void run() override { PANIC("not implemented"); }
  OptRef<Admin> admin() override {
    return makeOptRefFromPtr(static_cast<Envoy::Server::Admin*>(admin_.get()));
  }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override { return *config_.clusterManager(); }
  const Upstream::ClusterManager& clusterManager() const override {
    return *config_.clusterManager();
  }
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    Network::DnsResolverFactory& dns_resolver_factory =
        Network::createDefaultDnsResolverFactory(typed_dns_resolver_config);
    return dns_resolver_factory.createDnsResolver(dispatcher(), api(), typed_dns_resolver_config);
  }
  void drainListeners(OptRef<const Network::ExtraShutdownListenerOptions>) override {}
  DrainManager& drainManager() override { return *drain_manager_; }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  void failHealthcheck(bool) override {}
  HotRestart& hotRestart() override { return nop_hot_restart_; }
  Init::Manager& initManager() override { return init_manager_; }
  ServerLifecycleNotifier& lifecycleNotifier() override { return *this; }
  ListenerManager& listenerManager() override { return *listener_manager_; }
  Secret::SecretManager& secretManager() override { return *secret_manager_; }
  Runtime::Loader& runtime() override { return *runtime_; }
  void shutdown() override;
  bool isShutdown() override { return false; }
  void shutdownAdmin() override {}
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  OverloadManager& overloadManager() override { return *overload_manager_; }
  bool healthCheckFailed() override { return false; }
  const Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { PANIC("not implemented"); }
  time_t startTimeFirstEpoch() override { PANIC("not implemented"); }
  Stats::Store& stats() override { return stats_store_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  Router::Context& routerContext() override { return router_context_; }
  ProcessContextOptRef processContext() override { return api_->processContext(); }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  LocalInfo::LocalInfo& localInfo() const override { return *local_info_; }
  TimeSource& timeSource() override { return api_->timeSource(); }
  Envoy::MutexTracer* mutexTracer() override { return mutex_tracer_; }
  void flushStats() override {}
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }
  bool enableReusePortDefault() override { return true; }

  Configuration::StatsConfig& statsConfig() override { return config_.statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }
  Configuration::ServerFactoryContext& serverFactoryContext() override { return server_contexts_; }
  Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() override {
    return server_contexts_;
  }
  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }
  void setSinkPredicates(std::unique_ptr<Stats::SinkPredicates>&&) override {}

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
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  Api::ApiPtr api_;
  // ssl_context_manager_ must come before dispatcher_, since ClusterInfo
  // references SslSocketFactory and is deleted on the main thread via the dispatcher.
  std::unique_ptr<Ssl::ContextManager> ssl_context_manager_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<Server::ValidationAdmin> admin_;
  Singleton::ManagerPtr singleton_manager_;
  std::unique_ptr<Runtime::Loader> runtime_;
  Random::RandomGeneratorImpl random_generator_;
  Configuration::MainImpl config_;
  LocalInfo::LocalInfoPtr local_info_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::unique_ptr<Upstream::ValidationClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<ListenerManager> listener_manager_;
  std::unique_ptr<OverloadManager> overload_manager_;
  MutexTracer* mutex_tracer_{nullptr};
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  Event::TimeSystem& time_system_;
  ServerFactoryContextImpl server_contexts_;
  Quic::QuicStatNames quic_stat_names_;
  Filter::TcpListenerFilterConfigProviderManagerImpl tcp_listener_config_provider_manager_;
  Server::DrainManagerPtr drain_manager_;
  HotRestartNopImpl nop_hot_restart_;
};

} // namespace Server
} // namespace Envoy
