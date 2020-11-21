#include "server/config_validation/server.h"

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/event/real_time_system.h"
#include "common/local_info/local_info_impl.h"
#include "common/protobuf/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/version/version.h"

#include "server/ssl_context_manager.h"

namespace Envoy {
namespace Server {

bool validateConfig(const Options& options,
                    const Network::Address::InstanceConstSharedPtr& local_address,
                    ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                    Filesystem::Instance& file_system) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  try {
    Event::RealTimeSystem time_system;
    ValidationInstance server(options, time_system, local_address, stats_store, access_log_lock,
                              component_factory, thread_factory, file_system);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  } catch (const EnvoyException& e) {
    return false;
  }
}

ValidationInstance::ValidationInstance(
    const Options& options, Event::TimeSystem& time_system,
    const Network::Address::InstanceConstSharedPtr& local_address, Stats::IsolatedStoreImpl& store,
    Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
    Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system)
    : options_(options), validation_context_(options_.allowUnknownStaticFields(),
                                             !options.rejectUnknownDynamicFields(),
                                             !options.ignoreUnknownDynamicFields()),
      stats_store_(store), api_(new Api::ValidationImpl(thread_factory, store, time_system,
                                                        file_system, random_generator_)),
      dispatcher_(api_->allocateDispatcher("main_thread")),
      singleton_manager_(new Singleton::ManagerImpl(api_->threadFactory())),
      access_log_manager_(options.fileFlushIntervalMsec(), *api_, *dispatcher_, access_log_lock,
                          store),
      mutex_tracer_(nullptr), grpc_context_(stats_store_.symbolTable()),
      http_context_(stats_store_.symbolTable()), time_system_(time_system),
      server_contexts_(*this) {
  try {
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());
    shutdown();
    throw;
  }
}

void ValidationInstance::initialize(const Options& options,
                                    const Network::Address::InstanceConstSharedPtr& local_address,
                                    ComponentFactory& component_factory) {
  // See comments on InstanceImpl::initialize() for the overall flow here.
  //
  // For validation, we only do a subset of normal server initialization: everything that could fail
  // on a malformed config (e.g. JSON parsing and all the object construction that follows), but
  // more importantly nothing with observable effects (e.g. binding to ports or shutting down any
  // other Envoy process).
  //
  // If we get all the way through that stripped-down initialization flow, to the point where we'd
  // be ready to serve, then the config has passed validation.
  // Handle configuration that needs to take place prior to the main configuration load.
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  InstanceUtil::loadBootstrapConfig(bootstrap, options,
                                    messageValidationContext().staticValidationVisitor(), *api_);

  Config::Utility::createTagProducer(bootstrap);

  bootstrap.mutable_node()->set_hidden_envoy_deprecated_build_version(VersionInfo::version());

  local_info_ = std::make_unique<LocalInfo::LocalInfoImpl>(
      bootstrap.node(), local_address, options.serviceZone(), options.serviceClusterName(),
      options.serviceNodeName());

  Configuration::InitialImpl initial_config(bootstrap);
  overload_manager_ = std::make_unique<OverloadManagerImpl>(
      dispatcher(), stats(), threadLocal(), bootstrap.overload_manager(),
      messageValidationContext().staticValidationVisitor(), *api_);
  listener_manager_ = std::make_unique<ListenerManagerImpl>(*this, *this, *this, false);
  thread_local_.registerThread(*dispatcher_, true);
  runtime_singleton_ = std::make_unique<Runtime::ScopedLoaderSingleton>(
      component_factory.createRuntime(*this, initial_config));
  secret_manager_ = std::make_unique<Secret::SecretManagerImpl>(admin().getConfigTracker());
  ssl_context_manager_ = createContextManager("ssl_context_manager", api_->timeSource());
  cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
      admin(), runtime(), stats(), threadLocal(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo(), *secret_manager_, messageValidationContext(), *api_, http_context_,
      grpc_context_, accessLogManager(), singletonManager(), time_system_);
  config_.initialize(bootstrap, *this, *cluster_manager_factory_);
  runtime().initialize(clusterManager());
  clusterManager().setInitializedCb([this]() -> void { init_manager_.initialize(init_watcher_); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  thread_local_.shutdownGlobalThreading();
  if (config_.clusterManager() != nullptr) {
    config_.clusterManager()->shutdown();
  }
  thread_local_.shutdownThread();
}

} // namespace Server
} // namespace Envoy
