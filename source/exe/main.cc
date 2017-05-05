#include <iostream>
#include <memory>
#include <regex>

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

#if __cplusplus < 201103L ||                                                                       \
    (defined(__GLIBCXX__) && (__cplusplus < 201402L) &&                                            \
     (!defined(_GLIBCXX_REGEX_DFS_QUANTIFIERS_LIMIT) && !defined(_GLIBCXX_REGEX_STATE_LIMIT)))
#error "Your compiler does not support std::regex properly.  GCC 4.9+ or Clang required."
#endif

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

int main(int argc, char** argv) {
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  SignalAction handle_sigs;
#endif
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  OptionsImpl options(argc, argv, Server::SharedMemory::version(), spdlog::level::warn);

  std::unique_ptr<Server::HotRestartImpl> restarter;
  try {
    restarter.reset(new Server::HotRestartImpl(options));
  } catch (EnvoyException& e) {
    std::cerr << "unable to initialize hot restart: " << e.what() << std::endl;
    return 1;
  }

  Logger::Registry::initialize(options.logLevel(), restarter->logLock());
  DefaultTestHooks default_test_hooks;
  Stats::ThreadLocalStoreImpl stats_store(*restarter);
  Server::ProdComponentFactory component_factory;
  LocalInfo::LocalInfoImpl local_info(Network::Utility::getLocalAddress(), options.serviceZone(),
                                      options.serviceClusterName(), options.serviceNodeName());
  Server::InstanceImpl server(options, default_test_hooks, *restarter, stats_store,
                              restarter->accessLogLock(), component_factory, local_info);
  server.run();
  ares_library_cleanup();
  return 0;
}
