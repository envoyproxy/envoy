#include "server/config_validation/server.h"

#include <memory>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.validate.h"

#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/config/bootstrap_json.h"
#include "common/config/utility.h"
#include "common/event/real_time_system.h"
#include "common/local_info/local_info_impl.h"
#include "common/protobuf/utility.h"
#include "common/singleton/manager_impl.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {

bool validateConfig(Options& options, Network::Address::InstanceConstSharedPtr local_address,
                    ComponentFactory& component_factory) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  try {
    Event::RealTimeSystem time_system;
    ValidationInstance server(options, time_system, local_address, stats_store, access_log_lock,
                              component_factory);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  } catch (const EnvoyException& e) {
    return false;
  }
}

ValidationInstance::ValidationInstance(Options& options, Event::TimeSystem& time_system,
                                       Network::Address::InstanceConstSharedPtr local_address,
                                       Stats::IsolatedStoreImpl& store,
                                       Thread::BasicLockable& access_log_lock,
                                       ComponentFactory& component_factory)
    : options_(options), time_system_(time_system), stats_store_(store),
      api_(new Api::ValidationImpl(options.fileFlushIntervalMsec())),
      dispatcher_(api_->allocateDispatcher(time_system)),
      singleton_manager_(new Singleton::ManagerImpl()),
      access_log_manager_(*api_, *dispatcher_, access_log_lock, store) {
  try {
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());
    shutdown();
    throw;
  }
}

void ValidationInstance::initialize(Options& options,
                                    Network::Address::InstanceConstSharedPtr local_address,
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
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  InstanceUtil::loadBootstrapConfig(bootstrap, options);

  Config::Utility::createTagProducer(bootstrap);

  bootstrap.mutable_node()->set_build_version(VersionInfo::version());

  local_info_ = std::make_unique<LocalInfo::LocalInfoImpl>(
      bootstrap.node(), local_address, options.serviceZone(), options.serviceClusterName(),
      options.serviceNodeName());

  Configuration::InitialImpl initial_config(bootstrap);
  overload_manager_ = std::make_unique<OverloadManagerImpl>(dispatcher(), stats(), threadLocal(),
                                                            bootstrap.overload_manager());
  listener_manager_ = std::make_unique<ListenerManagerImpl>(*this, *this, *this, time_system_);
  thread_local_.registerThread(*dispatcher_, true);
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);
  secret_manager_ = std::make_unique<Secret::SecretManagerImpl>();
  ssl_context_manager_ = std::make_unique<Ssl::ContextManagerImpl>(time_system_);
  cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo(), *secret_manager_);

  Configuration::MainImpl* main_config = new Configuration::MainImpl();
  config_.reset(main_config);
  main_config->initialize(bootstrap, *this, *cluster_manager_factory_);

  clusterManager().setInitializedCb(
      [this]() -> void { init_manager_.initialize([]() -> void {}); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  thread_local_.shutdownGlobalThreading();
  if (config_ != nullptr && config_->clusterManager() != nullptr) {
    config_->clusterManager()->shutdown();
  }
  thread_local_.shutdownThread();
}

} // namespace Server
} // namespace Envoy
