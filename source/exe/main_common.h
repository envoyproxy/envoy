#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "common/common/thread.h"
#include "common/event/real_time_system.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "exe/platform_impl.h"

#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

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
  // Consumer must guarantee that all passed references are alive until this object is
  // destructed.
  MainCommonBase(const OptionsImpl& options, Event::TimeSystem& time_system, TestHooks& test_hooks,
                 Server::ComponentFactory& component_factory,
                 std::unique_ptr<Runtime::RandomGenerator>&& random_generator,
                 Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system);
  ~MainCommonBase();

  bool run();

  // Will be null if options.mode() == Server::Mode::Validate
  Server::Instance* server() { return server_.get(); }

  using AdminRequestFn =
      std::function<void(const Http::HeaderMap& response_headers, absl::string_view body)>;

  // Makes an admin-console request by path, calling handler() when complete.
  // The caller can initiate this from any thread, but it posts the request
  // onto the main thread, so the handler is called asynchronously.
  //
  // This is designed to be called from downstream consoles, so they can access
  // the admin console information stream without opening up a network port.
  //
  // This should only be called while run() is active; ensuring this is the
  // responsibility of the caller.
  //
  // TODO(jmarantz): consider std::future for encapsulating this delayed request
  // semantics, rather than a handler callback.
  void adminRequest(absl::string_view path_and_query, absl::string_view method,
                    const AdminRequestFn& handler);

protected:
  const Envoy::OptionsImpl& options_;
  Stats::FakeSymbolTableImpl symbol_table_;
  Server::ComponentFactory& component_factory_;
  Thread::ThreadFactory& thread_factory_;
  Filesystem::Instance& file_system_;

  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> stats_store_;
  std::unique_ptr<Logger::Context> logging_context_;
  std::unique_ptr<Server::InstanceImpl> server_;

private:
  void configureComponentLogLevels();
};

// TODO(jmarantz): consider removing this class; I think it'd be more useful to
// go through MainCommonBase directly.
class MainCommon {
public:
  MainCommon(int argc, const char* const* argv);
  bool run() { return base_.run(); }
  // Only tests have a legitimate need for this today.
  Event::Dispatcher& dispatcherForTest() { return base_.server()->dispatcher(); }

  // Makes an admin-console request by path, calling handler() when complete.
  // The caller can initiate this from any thread, but it posts the request
  // onto the main thread, so the handler is called asynchronously.
  //
  // This is designed to be called from downstream consoles, so they can access
  // the admin console information stream without opening up a network port.
  //
  // This should only be called while run() is active; ensuring this is the
  // responsibility of the caller.
  void adminRequest(absl::string_view path_and_query, absl::string_view method,
                    const MainCommonBase::AdminRequestFn& handler) {
    base_.adminRequest(path_and_query, method, handler);
  }

  static std::string hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                       bool hot_restart_enabled);

private:
#ifdef ENVOY_HANDLE_SIGNALS
  Envoy::SignalAction handle_sigs;
  Envoy::TerminateHandler log_on_terminate;
#endif

  PlatformImpl platform_impl_;
  Envoy::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  DefaultTestHooks default_test_hooks_;
  ProdComponentFactory prod_component_factory_;
  MainCommonBase base_;
};

} // namespace Envoy
