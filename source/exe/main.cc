#include "common/event/libevent.h"
#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"
#include "common/ssl/openssl.h"
#include "common/stats/thread_local_store.h"
#include "exe/hot_restart.h"
#include "server/drain_manager_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "ares.h"

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
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  Ssl::OpenSsl::initialize();
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
  restarter.reset(); // The restarter needs to be deleted before the server.
  return 0;
}
