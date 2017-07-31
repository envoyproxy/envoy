#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/event/libevent.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"
#include "common/stats/thread_local_store.h"

#ifdef ENVOY_HOT_RESTART
#include "exe/hot_restart.h"
#endif
#include "exe/hot_restart_nop.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "ares.h"

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

int main_common(OptionsImpl& options) {
#ifdef ENVOY_HOT_RESTART
  std::unique_ptr<Envoy::Server::HotRestartImpl> restarter;
  try {
    restarter.reset(new Server::HotRestartImpl(options));
  } catch (Envoy::EnvoyException& e) {
    std::cerr << "unable to initialize hot restart: " << e.what() << std::endl;
    return 1;
  }

  Thread::BasicLockable& log_lock = restarter->logLock();
  Thread::BasicLockable& access_log_lock = restarter->accessLogLock();
  Stats::RawStatDataAllocator &stats_allocator = *restarter;
#else
  std::unique_ptr<Envoy::Server::HotRestartNopImpl> restarter;
  restarter.reset(new Server::HotRestartNopImpl());

  Thread::MutexBasicLockable log_lock, access_log_lock;
  Stats::HeapRawStatDataAllocator stats_allocator;
#endif

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

  Logger::Registry::initialize(options.logLevel(), log_lock);
  DefaultTestHooks default_test_hooks;
  ThreadLocal::InstanceImpl tls;
  Stats::ThreadLocalStoreImpl stats_store(stats_allocator);
  Server::InstanceImpl server(options, default_test_hooks, *restarter, stats_store,
                              access_log_lock, component_factory, local_info, tls);
  server.run();
  ares_library_cleanup();
  return 0;
}

} // namespace Envoy
