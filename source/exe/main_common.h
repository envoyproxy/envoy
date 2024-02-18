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
  class AdminResponse : public std::enable_shared_from_this<AdminResponse> {
  public:
    virtual ~AdminResponse() = default;

    /**
     * Requests the headers for the response. This can be called from any
     * thread, and HeaderFn may also be called from any thread.
     *
     * HeadersFn will not be called after cancel(). It is invalid to
     * to call nextChunk from within HeadersFn -- the caller must trigger
     * such a call on another thread, after HeadersFn returns. Calling
     * nextChunk from HeadersFn may deadlock.
     *
     * If the server is shut down during the operation, headersFn will
     * be called with a 503.
     *
     * It is a programming error to call getHeaders after calling cancel.
     *
     * @param fn The function to be called with the headers and status code.
     */
    using HeadersFn = std::function<void(Http::Code, Http::ResponseHeaderMap& map)>;
    virtual void getHeaders(HeadersFn fn) PURE;

    /**
     * Requests a new chunk. This can be called from any thread, and the BodyFn
     * callback may also be called from any thread. BodyFn will be called in a
     * loop until the Buffer passed to it is fully drained. When 'false' is
     * passed as the second arg to BodyFn, that signifies the end of the
     * response, and nextChunk must not be called again.
     *
     * BodyFn will not be called after cancel(). It is invalid to
     * to call nextChunk from within BodyFn -- the caller must trigger
     * such a call on another thread, after BodyFn returns. Calling
     * nextChunk from BodyFn may deadlock.
     *
     * If the server is shut down during the operation, bodyFn will
     * be called with an empty body and 'false' for more_data.
     *
     * It is a programming error to call nextChunk after calling cancel.
     *
     * @param fn A function to be called on each chunk.
     */
    using BodyFn = std::function<void(Buffer::Instance&, bool)>;
    virtual void nextChunk(BodyFn fn) PURE;

    /**
     * Requests that any outstanding callbacks be dropped. This can be called
     * when the context in which the request is made is destroyed. This can be
     * useful to allow an application above to register a timeout. The Response
     * itself is held as a shared_ptr as that makes it much easier to manage
     * cancellation across multiple threads.
     */
    virtual void cancel() PURE;

    // Called when the server is terminated. The response remains
    // valid after this.
    virtual void terminate() PURE;
  };
  using AdminResponseSharedPtr = std::shared_ptr<AdminResponse>;

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
  AdminResponseSharedPtr adminRequest(absl::string_view path_and_query, absl::string_view method);

  // Called when a streaming response is terminated, either by completing normally
  // or having the caller call cancel on it. Either way it needs to be removed from
  // the set that will be used by terminateAdminRequests below.
  void detachResponse(AdminResponse*);

private:
  // Called after the server run-loop finishes; any outstanding streaming admin requests
  // will otherwise hang as the main-thread dispatcher loop will no longer run.
  void terminateAdminRequests();

  absl::Mutex mutex_;
  absl::flat_hash_set<AdminResponse*> response_set_ ABSL_GUARDED_BY(mutex_);
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
  MainCommonBase::AdminResponseSharedPtr adminRequest(absl::string_view path_and_query,
                                                      absl::string_view method) {
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
