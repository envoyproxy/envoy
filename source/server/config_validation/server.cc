#include "source/server/config_validation/server.h"

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/event/real_time_system.h"
#include "source/common/local_info/local_info_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/version/version.h"
#include "source/server/ssl_context_manager.h"

namespace Envoy {
namespace Server {

bool validateConfig(const Options& options,
                    const Network::Address::InstanceConstSharedPtr& local_address,
                    ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                    Filesystem::Instance& file_system) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  TRY_ASSERT_MAIN_THREAD {
    Event::RealTimeSystem time_system;
    ValidationInstance server(options, time_system, local_address, stats_store, access_log_lock,
                              component_factory, thread_factory, file_system);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  }
  END_TRY
  catch (const EnvoyException& e) {
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
      stats_store_(store),
      api_(new Api::ValidationImpl(thread_factory, store, time_system, file_system,
                                   random_generator_, bootstrap_)),
      dispatcher_(api_->allocateDispatcher("main_thread")),
      singleton_manager_(new Singleton::ManagerImpl(api_->threadFactory())),
      access_log_manager_(options.fileFlushIntervalMsec(), *api_, *dispatcher_, access_log_lock,
                          store),
      mutex_tracer_(nullptr), grpc_context_(stats_store_.symbolTable()),
      http_context_(stats_store_.symbolTable()), router_context_(stats_store_.symbolTable()),
      time_system_(time_system), server_contexts_(*this),
      quic_stat_names_(stats_store_.symbolTable()) {
  TRY_ASSERT_MAIN_THREAD { initialize(options, local_address, component_factory); }
  END_TRY
  catch (const EnvoyException& e) {
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
  InstanceUtil::loadBootstrapConfig(bootstrap_, options,
                                    messageValidationContext().staticValidationVisitor(), *api_);

  Config::Utility::createTagProducer(bootstrap_, options_.statsTags());
  if (!bootstrap_.node().user_agent_build_version().has_version()) {
    *bootstrap_.mutable_node()->mutable_user_agent_build_version() = VersionInfo::buildVersion();
  }

  local_info_ = std::make_unique<LocalInfo::LocalInfoImpl>(
      stats().symbolTable(), bootstrap_.node(), bootstrap_.node_context_params(), local_address,
      options.serviceZone(), options.serviceClusterName(), options.serviceNodeName());

  overload_manager_ = std::make_unique<OverloadManagerImpl>(
      dispatcher(), stats(), threadLocal(), bootstrap_.overload_manager(),
      messageValidationContext().staticValidationVisitor(), *api_, options_);
  Configuration::InitialImpl initial_config(bootstrap_);
  initial_config.initAdminAccessLog(bootstrap_, *this);
  admin_ = std::make_unique<Server::ValidationAdmin>(initial_config.admin().address());
  listener_manager_ =
      std::make_unique<ListenerManagerImpl>(*this, *this, *this, false, quic_stat_names_);
  thread_local_.registerThread(*dispatcher_, true);

  Runtime::LoaderPtr runtime_ptr = component_factory.createRuntime(*this, initial_config);
  if (runtime_ptr->snapshot().getBoolean("envoy.restart_features.remove_runtime_singleton", true)) {
    runtime_ = std::move(runtime_ptr);
  } else {
    runtime_singleton_ = std::make_unique<Runtime::ScopedLoaderSingleton>(std::move(runtime_ptr));
  }

  secret_manager_ = std::make_unique<Secret::SecretManagerImpl>(admin()->getConfigTracker());
  ssl_context_manager_ = createContextManager("ssl_context_manager", api_->timeSource());
  cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
      admin(), runtime(), stats(), threadLocal(),
      [this]() -> Network::DnsResolverSharedPtr { return this->dnsResolver(); },
      sslContextManager(), dispatcher(), localInfo(), *secret_manager_, messageValidationContext(),
      *api_, http_context_, grpc_context_, router_context_, accessLogManager(), singletonManager(),
      options, quic_stat_names_, *this);
  config_.initialize(bootstrap_, *this, *cluster_manager_factory_);
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
  dispatcher_->shutdown();
}

} // namespace Server
} // namespace Envoy
