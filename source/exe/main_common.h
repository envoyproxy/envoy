#pragma once

#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/options_impl.h"
#include "server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#include "exe/terminate_handler.h"
#endif

namespace Envoy {

class ProdComponentFactory : public Server::ComponentFactory {
public:
  // Server::DrainManagerFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance& server) override;
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override;
};

class MainCommonBase {
public:
  MainCommonBase(OptionsImpl& options);
  ~MainCommonBase();

  bool run();

protected:
  Envoy::OptionsImpl& options_;
  ProdComponentFactory component_factory_;
  DefaultTestHooks default_test_hooks_;
  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> stats_store_;
  std::unique_ptr<Server::InstanceImpl> server_;
};

class MainCommon {
public:
  MainCommon(int argc, const char* const* argv);
  bool run() { return base_.run(); }

  static std::string hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                       bool hot_restart_enabled);

private:
#ifdef ENVOY_HANDLE_SIGNALS
  Envoy::SignalAction handle_sigs;
  Envoy::TerminateHandler log_on_terminate;
#endif

  Envoy::OptionsImpl options_;
  MainCommonBase base_;
};

/**
 * This is the real main body that executes after site-specific
 * main() runs.
 *
 * @param options Options object initialized by site-specific code
 * @return int Return code that should be returned from the actual main()
 */
int main_common(OptionsImpl& options);

} // namespace Envoy
