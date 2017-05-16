#include "server/config_validation/server.h"

#include "common/json/config_schemas.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {

ValidationInstance::ValidationInstance(Options& options, HotRestart& restarter,
                                       Stats::StoreRoot& store,
                                       Thread::BasicLockable& access_log_lock,
                                       ComponentFactory& component_factory,
                                       const LocalInfo::LocalInfo& local_info)
    : options_(options), restarter_(restarter), stats_store_(store),
      handler_(Api::ApiPtr{new Api::ValidationImpl(options.fileFlushIntervalMsec())}),
      local_info_(local_info),
      access_log_manager_(handler_.api(), handler_.dispatcher(), access_log_lock, store) {
  try {
    initialize(options, component_factory);
  } catch (const EnvoyException& e) {
    log().critical("error initializing configuration '{}': {}", options.configPath(), e.what());
    thread_local_.shutdownThread();
    exit(1);
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
  Json::ObjectPtr config_json = Json::Factory::loadFromFile(options.configPath());
  config_json->validateSchema(Json::Schema::TOP_LEVEL_CONFIG_SCHEMA);
  Configuration::InitialImpl initial_config(*config_json);

  for (uint32_t i = 0; i < std::max(1U, options.concurrency()); i++) {
    workers_.emplace_back(new Worker(thread_local_, options.fileFlushIntervalMsec()));
  }
  thread_local_.registerThread(handler_.dispatcher(), true);
  stats_store_.initializeThreading(handler_.dispatcher(), thread_local_);
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));
  cluster_manager_factory_.reset(new Upstream::ValidationClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo()));

  Configuration::MainImpl* main_config = new Configuration::MainImpl(*this);
  config_.reset(main_config);
  main_config->initialize(*config_json);

  clusterManager().setInitializedCb([this]()
                                        -> void { init_manager_.initialize([]() -> void {}); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  stats_store_.shutdownThreading();
  config_->clusterManager().shutdown();
  thread_local_.shutdownThread();
}

} // Server
} // Envoy
