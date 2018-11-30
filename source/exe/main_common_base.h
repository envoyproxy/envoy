#pragma once

#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread/thread.h"

#include "common/event/real_time_system.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

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
  MainCommonBase(OptionsImpl& options, Thread::ThreadFactory& thread_factory);
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
  Envoy::OptionsImpl& options_;
  Thread::ThreadFactory& thread_factory_;

  std::unique_ptr<Envoy::Runtime::RandomGeneratorImpl> random_generator_;
  Event::RealTimeSystem real_time_system_;
  DefaultTestHooks default_test_hooks_;
  ProdComponentFactory prod_component_factory_;

  std::unique_ptr<ThreadLocal::InstanceImpl> tls_;
  std::unique_ptr<Server::HotRestart> restarter_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> stats_store_;
  std::unique_ptr<Logger::Context> logging_context_;
  std::unique_ptr<Server::InstanceImpl> server_;

private:
  void configureComponentLogLevels();
};

} // namespace Envoy
