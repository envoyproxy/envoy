#include "server/config_validation/server.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {

bool validateConfig(Options& options, ComponentFactory& component_factory,
                    const LocalInfo::LocalInfo& local_info) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  try {
    ValidationInstance server(options, stats_store, access_log_lock, component_factory, local_info);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  } catch (const EnvoyException& e) {
    return false;
  }
}

ValidationInstance::ValidationInstance(Options& options, Stats::IsolatedStoreImpl& store,
                                       Thread::BasicLockable& access_log_lock,
                                       ComponentFactory& component_factory,
                                       const LocalInfo::LocalInfo& local_info)
    : options_(options), stats_store_(store),
      handler_(Api::ApiPtr{new Api::ValidationImpl(options.fileFlushIntervalMsec())}),
      local_info_(local_info),
      access_log_manager_(handler_.api(), handler_.dispatcher(), access_log_lock, store) {
  try {
    initialize(options, component_factory);
  } catch (const EnvoyException& e) {
    LOG(critical, "error initializing configuration '{}': {}", options.configPath(), e.what());
    thread_local_.shutdownThread();
    throw;
  }
}

void ValidationInstance::initialize(Options& options, ComponentFactory& component_factory) {
  // See comments on InstanceImpl::initialize() for the overall flow here.
  //
  // For validation, we only do a subset of normal server initialization: everything that could fail
  // on a malformed config (e.g. JSON parsing and all the object construction that follows), but
  // more importantly nothing with observable effects (e.g. binding to ports or shutting down any
  // other Envoy process).
  //
  // If we get all the way through that stripped-down initialization flow, to the point where we'd
  // be ready to serve, then the config has passed validation.
  Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(options.configPath());
  Configuration::InitialImpl initial_config(*config_json);
  thread_local_.registerThread(handler_.dispatcher(), true);
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));
  cluster_manager_factory_.reset(new Upstream::ValidationClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo()));

  Configuration::MainImpl* main_config =
      new Configuration::MainImpl(*this, *cluster_manager_factory_);
  config_.reset(main_config);
  main_config->initialize(*config_json);

  clusterManager().setInitializedCb([this]()
                                        -> void { init_manager_.initialize([]() -> void {}); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  config_->clusterManager().shutdown();
  thread_local_.shutdownThread();
}

} // Server
} // Envoy
