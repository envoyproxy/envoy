#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/event/libevent.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"

#include "exe/hot_restart.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

#include "server/drain_manager_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "ares.h"
#include "spdlog/spdlog.h"

namespace Lyft {
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
} // Lyft

int main(int argc, char** argv) {
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Lyft::SignalAction handle_sigs;
#endif
  ares_library_init(ARES_LIB_INIT_ALL);
  Lyft::Event::Libevent::Global::initialize();
  Lyft::OptionsImpl options(argc, argv, Lyft::Server::SharedMemory::version(), spdlog::level::warn);

  std::unique_ptr<Lyft::Server::HotRestartImpl> restarter;
  try {
    restarter.reset(new Lyft::Server::HotRestartImpl(options));
  } catch (Lyft::EnvoyException& e) {
    std::cerr << "unable to initialize hot restart: " << e.what() << std::endl;
    return 1;
  }

  Logger::Registry::initialize(options.logLevel(), restarter->logLock());
  DefaultTestHooks default_test_hooks;
  Stats::ThreadLocalStoreImpl stats_store(*restarter);
  Server::ProdComponentFactory component_factory;
  // TODO(henna): Add CLI option for local address IP version.
  LocalInfo::LocalInfoImpl local_info(
      Network::Utility::getLocalAddress(Network::Address::IpVersion::v4), options.serviceZone(),
      options.serviceClusterName(), options.serviceNodeName());
  Server::InstanceImpl server(options, default_test_hooks, *restarter, stats_store,
                              restarter->accessLogLock(), component_factory, local_info);
  server.run();
  ares_library_cleanup();
  return 0;
}
