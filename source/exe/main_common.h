#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"

#include "common/common/thread.h"
#include "common/event/real_time_system.h"
#include "common/grpc/google_grpc_context.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "exe/platform_impl.h"
#include "exe/process_wide.h"

#include "server/listener_hooks.h"
#include "server/options_impl.h"
#include "server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
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
  MainCommonBase(const OptionsImpl& options, Event::TimeSystem& time_system,
                 ListenerHooks& listener_hooks, Server::ComponentFactory& component_factory,
                 std::unique_ptr<Random::RandomGenerator>&& random_generator,
                 Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
                 std::unique_ptr<ProcessContext> process_context);

  bool run();

  // Will be null if options.mode() == Server::Mode::Validate
  Server::Instance* server() { return server_.get(); }

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

protected:
  ProcessWide process_wide_; // Process-wide state setup/teardown (excluding grpc).
  // We instantiate this class regardless of ENVOY_GOOGLE_GRPC, to avoid having
  // an ifdef in a header file exposed in a C++ library. It is too easy to have
  // the ifdef be inconsistent across build-system boundaries.
  Grpc::GoogleGrpcContext google_grpc_context_;
  const Envoy::OptionsImpl& options_;
  Server::ComponentFactory& component_factory_;
  Thread::ThreadFactory& thread_factory_;
  Filesystem::Instance& file_system_;
  Stats::SymbolTablePtr symbol_table_;
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
};

// TODO(jmarantz): consider removing this class; I think it'd be more useful to
// go through MainCommonBase directly.
class MainCommon {
public:
  // Hook to run after a server is created.
  using PostServerHook = std::function<void(Server::Instance& server)>;

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
#ifdef ENVOY_HANDLE_SIGNALS
  Envoy::SignalAction handle_sigs_;
  Envoy::TerminateHandler log_on_terminate_;
#endif

  PlatformImpl platform_impl_;
  Envoy::OptionsImpl options_;
  Event::RealTimeSystem real_time_system_;
  DefaultListenerHooks default_listener_hooks_;
  ProdComponentFactory prod_component_factory_;
  MainCommonBase base_;
};

} // namespace Envoy
