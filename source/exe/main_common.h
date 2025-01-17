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

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/exe/admin_response.h"
#endif
#include "source/exe/process_wide.h"
#include "source/exe/stripped_main_base.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl.h"
#include "source/server/server.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#include "source/exe/terminate_handler.h"
#endif

namespace Envoy {

class MainCommonBase : public StrippedMainBase {
public:
  MainCommonBase(const Server::Options& options, Event::TimeSystem& time_system,
                 ListenerHooks& listener_hooks, Server::ComponentFactory& component_factory,
                 std::unique_ptr<Server::Platform> platform_impl,
                 std::unique_ptr<Random::RandomGenerator>&& random_generator,
                 std::unique_ptr<ProcessContext> process_context);

  bool run();

#ifdef ENVOY_ADMIN_FUNCTIONALITY
  using AdminRequestFn =
      std::function<void(const Http::ResponseHeaderMap& response_headers, absl::string_view body)>;

  /**
   * Makes an admin-console request by path, calling handler() when complete.
   * The caller can initiate this from any thread, but it posts the request
   * onto the main thread, so the handler is called asynchronously.
   *
   * This is designed to be called from downstream consoles, so they can access
   * the admin console information stream without opening up a network port.
   *
   * This should only be called while run() is active; ensuring this is the
   * responsibility of the caller.
   *
   * TODO(jmarantz): consider std::future for encapsulating this delayed request
   * semantics, rather than a handler callback.
   *
   * Consider using the 2-arg version of adminRequest, below, which enables
   * streaming of large responses one chunk at a time, without holding
   * potentially huge response text in memory.
   *
   * @param path_and_query the URL to send to admin, including any query params.
   * @param method the HTTP method: "GET" or "POST"
   * @param handler an async callback that will be sent the serialized headers
   *        and response.
   */
  void adminRequest(absl::string_view path_and_query, absl::string_view method,
                    const AdminRequestFn& handler);

  /**
   * Initiates a streaming response to an admin request. The caller interacts
   * with the returned AdminResponse object, and can thus control the pace of
   * handling chunks of response text.
   *
   * @param path_and_query the URL to send to admin, including any query params.
   * @param method the HTTP method: "GET" or "POST"
   * @return AdminResponseSharedPtr the response object
   */
  AdminResponseSharedPtr adminRequest(absl::string_view path_and_query, absl::string_view method);

private:
  AdminResponse::SharedPtrSet shared_response_set_;
#endif
};

// This is separate from MainCommonBase for legacy reasons: sufficient
// downstream tests use one or the other that resolving is deemed problematic.
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
  AdminResponseSharedPtr adminRequest(absl::string_view path_and_query, absl::string_view method) {
    return base_.adminRequest(path_and_query, method);
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
