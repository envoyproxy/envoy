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

} // namespace Server

int main_common(OptionsImpl& options, Server::HotRestartImpl& restarter) {
  Event::Libevent::Global::initialize();
  Server::ProdComponentFactory component_factory;
  LocalInfo::LocalInfoImpl local_info(
      Network::Utility::getLocalAddress(options.localAddressIpVersion()), options.serviceZone(),
      options.serviceClusterName(), options.serviceNodeName());

  switch (options.mode()) {
  case Server::Mode::Serve:
    break;
  case Server::Mode::Validate:
    Thread::MutexBasicLockable log_lock;
    Logger::Registry::initialize(options.logLevel(), log_lock);
    return Server::validateConfig(options, component_factory, local_info) ? 0 : 1;
  }

  ares_library_init(ARES_LIB_INIT_ALL);

  Logger::Registry::initialize(options.logLevel(), restarter.logLock());
  DefaultTestHooks default_test_hooks;
  Stats::ThreadLocalStoreImpl stats_store(restarter);
  Server::InstanceImpl server(options, default_test_hooks, restarter, stats_store,
                              restarter.accessLogLock(), component_factory, local_info);
  server.run();
  ares_library_cleanup();
  return 0;
}

} // namespace Envoy
