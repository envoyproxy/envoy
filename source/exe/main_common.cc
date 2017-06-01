#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/event/libevent.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"
#include "common/stats/thread_local_store.h"

#include "exe/hot_restart.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "ares.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

class ProdComponentFactory : public ComponentFactory {
public:
  // Server::DrainManagerFactory
  DrainManagerPtr createDrainManager(Instance& server) override {
    return DrainManagerPtr{new DrainManagerImpl(server)};
  }

  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override {
    return Server::InstanceUtil::createRuntime(server, config);
  }
};

} // Server

int main_common(Envoy::OptionsImpl& options, Envoy::Server::HotRestartImpl& restarter) {
  Envoy::Event::Libevent::Global::initialize();
  Envoy::Server::ProdComponentFactory component_factory;
  Envoy::LocalInfo::LocalInfoImpl local_info(
      Envoy::Network::Utility::getLocalAddress(Envoy::Network::Address::IpVersion::v4),
      options.serviceZone(), options.serviceClusterName(), options.serviceNodeName());

  switch (options.mode()) {
  case Envoy::Server::Mode::Serve:
    break;
  case Envoy::Server::Mode::Validate:
    Envoy::Thread::MutexBasicLockable log_lock;
    Envoy::Logger::Registry::initialize(options.logLevel(), log_lock);
    return Envoy::Server::validateConfig(options, component_factory, local_info) ? 0 : 1;
  }

  ares_library_init(ARES_LIB_INIT_ALL);

  Envoy::Logger::Registry::initialize(options.logLevel(), restarter.logLock());
  Envoy::DefaultTestHooks default_test_hooks;
  Envoy::Stats::ThreadLocalStoreImpl stats_store(restarter);
  // TODO(henna): Add CLI option for local address IP version.
  Envoy::Server::InstanceImpl server(options, default_test_hooks, restarter, stats_store,
                                     restarter.accessLogLock(), component_factory, local_info);
  server.run();
  ares_library_cleanup();
  return 0;
}

} // Envoy
