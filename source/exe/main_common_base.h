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

// This is the common main between Envoy and Envoy mobile.
// It should only include functionality required by Envoy Mobile: anything
// server-specific should live in MainCommon.
class MainCommonBase {
public:
  static std::string hotRestartVersion(bool hot_restart_enabled);

  // Consumer must guarantee that all passed references are alive until this object is
  // destructed.
  MainCommonBase(const Server::Options& options, Event::TimeSystem& time_system,
                 ListenerHooks& listener_hooks, Server::ComponentFactory& component_factory,
                 Server::Platform& platform_impl,
                 std::unique_ptr<Random::RandomGenerator>&& random_generator,
                 std::unique_ptr<ProcessContext> process_context);

  void runServer() {
    ASSERT(options_.mode() == Server::Mode::Serve);
    server_->run();
  }

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

} // namespace Envoy
