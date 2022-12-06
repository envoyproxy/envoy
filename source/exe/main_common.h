#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/platform.h"

#include "source/common/common/thread.h"
#include "source/common/event/real_time_system.h"
#include "source/common/grpc/google_grpc_context.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/exe/process_wide.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl.h"
#include "source/server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#include "source/exe/terminate_handler.h"
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
  MainCommonBase(const Server::Options& options, Event::TimeSystem& time_system,
                 ListenerHooks& listener_hooks, Server::ComponentFactory& component_factory,
                 std::unique_ptr<Server::Platform> platform_impl,
                 std::unique_ptr<Random::RandomGenerator>&& random_generator,
                 std::unique_ptr<ProcessContext> process_context);

  bool run();

  // Will be null if options.mode() == Server::Mode::Validate
  Server::Instance* server() { return server_.get(); }

#ifdef ENVOY_ADMIN_FUNCTIONALITY
  using AdminRequestFn =
      std::function<void(const Http::ResponseHeaderMap& response_headers, absl::string_view body)>;

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
#endif

protected:
  std::unique_ptr<Server::Platform> platform_impl_;
  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  // We instantiate this class regardless of ENVOY_GOOGLE_GRPC, to avoid having
  // an ifdef in a header file exposed in a C++ library. It is too easy to have
  // the ifdef be inconsistent across build-system boundaries.
  Grpc::GoogleGrpcContext google_grpc_context_;
  const Envoy::Server::Options& options_;
  Server::ComponentFactory& component_factory_;
  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl stats_allocator_;

  ThreadLocal::InstanceImplPtr tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  Stats::ThreadLocalStoreImplPtr stats_store_;
  std::unique_ptr<Logger::Context> logging_context_;
  std::unique_ptr<Init::Manager> init_manager_{std::make_unique<Init::ManagerImpl>("Server")};
  std::unique_ptr<Server::InstanceImpl> server_;

private:
  void configureComponentLogLevels();
  void configureHotRestarter(Random::RandomGenerator& random_generator);

  // Declaring main thread here allows custom integrations to instantiate
  // MainCommonBase directly, with environment-specific dependency injection.
  // Note that MainThread must also be declared in MainCommon.
  Thread::MainThread main_thread_;
};

// TODO(jmarantz): consider removing this class; I think it'd be more useful to
// go through MainCommonBase directly.
class MainCommon {
public:
  // Hook to run after a server is created.
  using PostServerHook = std::function<void(Server::Instance& server)>;

  MainCommon(int argc, const char* const* argv);
  MainCommon(const std::vector<std::string>& args);

  bool run() { return base_.run(); }
  // Only tests have a legitimate need for this today.
  Event::Dispatcher& dispatcherForTest() { return base_.server()->dispatcher(); }

#ifdef ENVOY_ADMIN_FUNCTIONALITY
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
#endif

  static std::string hotRestartVersion(bool hot_restart_enabled);

  /**
   * @return a pointer to the server instance, or nullptr if initialized into
   *         validation mode.
   */
  Server::Instance* server() { return base_.server(); }

  /**
   * Instantiates a MainCommon using default factory implements, parses args,
   * and runs an event loop depending on the mode.
   *
   * Note that MainCommonBase can also be directly instantiated, providing the
   * opportunity to override subsystem implementations for custom
   * implementations.
   *
   * @param argc number of command-line args
   * @param argv command-line argument array
   * @param hook optional hook to run after a server is created
   */
  static int main(int argc, char** argv, PostServerHook hook = nullptr);

private:
  Thread::MainThread main_thread_;

#ifdef ENVOY_HANDLE_SIGNALS
  Envoy::SignalAction handle_sigs_;
  Envoy::TerminateHandler log_on_terminate_;
#endif

  Envoy::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  DefaultListenerHooks default_listener_hooks_;
  ProdComponentFactory prod_component_factory_;
  MainCommonBase base_;
};

} // namespace Envoy
