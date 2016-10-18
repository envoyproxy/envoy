#include "hot_restart.h"

#include "common/event/libevent.h"
#include "common/ssl/openssl.h"
#include "server/drain_manager_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

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
  Event::Libevent::Global::initialize();
  Ssl::OpenSsl::initialize();
  OptionsImpl options(argc, argv, Server::SharedMemory::version(), spdlog::level::err);

  std::unique_ptr<Server::HotRestartImpl> restarter;
  try {
    restarter.reset(new Server::HotRestartImpl(options));
  } catch (EnvoyException& e) {
    std::cerr << "unable to initialize hot restart: " << e.what() << std::endl;
    return 1;
  }

  Logger::Registry::initialize(options.logLevel(), restarter->logLock());
  DefaultTestHooks default_test_hooks;
  Stats::ThreadLocalStoreImpl stats_store(restarter->statLock(), *restarter);
  Server::ProdComponentFactory component_factory;
  Server::InstanceImpl server(options, default_test_hooks, *restarter, stats_store,
                              restarter->accessLogLock(), component_factory);
  server.run();
  return 0;
}
