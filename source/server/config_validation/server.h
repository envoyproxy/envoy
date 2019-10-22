#pragma once

#include <iostream>

#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_manager_impl.h"
#include "common/common/assert.h"
#include "common/grpc/common.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/router/rds_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/config_validation/admin.h"
#include "server/config_validation/api.h"
#include "server/config_validation/cluster_manager.h"
#include "server/config_validation/dns.h"
#include "server/http/admin.h"
#include "server/listener_manager_impl.h"
#include "server/server.h"

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
  Admin& admin() override { return admin_; }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override { return *config_.clusterManager(); }
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override {
    return dispatcher().createDnsResolver({});
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
  Runtime::RandomGenerator& random() override { return random_generator_; }
  Runtime::Loader& runtime() override { return *runtime_loader_; }
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
  OptProcessContextRef processContext() override { return absl::nullopt; }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  const LocalInfo::LocalInfo& localInfo() const override { return *local_info_; }
  TimeSource& timeSource() override { return api_->timeSource(); }
  Envoy::MutexTracer* mutexTracer() override { return mutex_tracer_; }
  std::chrono::milliseconds statsFlushInterval() const override {
    return config_.statsFlushInterval();
  }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }
  Configuration::ServerFactoryContext& serverFactoryContext() override { return server_context_; }

  // Server::ListenerComponentFactory
  LdsApiPtr createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    return std::make_unique<LdsApiImpl>(lds_config, clusterManager(), initManager(), stats(),
                                        listenerManager(),
                                        messageValidationContext().dynamicValidationVisitor());
  }
  std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context) override {
    return ProdListenerComponentFactory::createNetworkFilterFactoryList_(filters, context);
  }
  std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createListenerFilterFactoryList_(filters, context);
  }
  std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(filters, context);
  }
  Network::SocketSharedPtr createListenSocket(Network::Address::InstanceConstSharedPtr,
                                              Network::Address::SocketType,
                                              const Network::Socket::OptionsSharedPtr&,
                                              bool) override {
    // Returned sockets are not currently used so we can return nothing here safely vs. a
    // validation mock.
    return nullptr;
  }
  DrainManagerPtr createDrainManager(envoy::api::v2::Listener::DrainType) override {
    return nullptr;
  }
  uint64_t nextListenerTag() override { return 0; }

  // Server::WorkerFactory
  WorkerPtr createWorker(OverloadManager&, const std::string&) override {
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
  Server::ValidationAdmin admin_;
  Singleton::ManagerPtr singleton_manager_;
  Runtime::LoaderPtr runtime_loader_;
  Runtime::RandomGeneratorImpl random_generator_;
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
  Event::TimeSystem& time_system_;
  ServerFactoryContextImpl server_context_;
};

} // namespace Server
} // namespace Envoy
