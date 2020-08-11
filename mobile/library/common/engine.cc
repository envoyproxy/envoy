#include "library/common/engine.h"

#include "common/common/lock_guard.h"

namespace Envoy {

Engine::Engine(envoy_engine_callbacks callbacks, const char* config, const char* log_level,
               std::atomic<envoy_network_t>& preferred_network)
    : callbacks_(callbacks) {
  // Ensure static factory registration occurs on time.
  // TODO: ensure this is only called one time once multiple Engine objects can be allocated.
  // https://github.com/lyft/envoy-mobile/issues/332
  ExtensionRegistry::registerFactories();

  // Create the Http::Dispatcher first since it contains initial queueing logic.
  // TODO: consider centralizing initial queueing in this class.
  // https://github.com/lyft/envoy-mobile/issues/720
  http_dispatcher_ = std::make_unique<Http::Dispatcher>(preferred_network);

  // Start the Envoy on a dedicated thread.
  main_thread_ = std::thread(&Engine::run, this, std::string(config), std::string(log_level));
}

envoy_status_t Engine::run(std::string config, std::string log_level) {
  {
    Thread::LockGuard lock(mutex_);
    try {
      char* envoy_argv[] = {strdup("envoy"), strdup("--config-yaml"),   strdup(config.c_str()),
                            strdup("-l"),    strdup(log_level.c_str()), nullptr};

      main_common_ = std::make_unique<MobileMainCommon>(5, envoy_argv);
      event_dispatcher_ = &main_common_->server()->dispatcher();
      cv_.notifyOne();
    } catch (const Envoy::NoServingException& e) {
      return ENVOY_FAILURE;
    } catch (const Envoy::MalformedArgvException& e) {
      std::cerr << e.what() << std::endl;
      return ENVOY_FAILURE;
    } catch (const Envoy::EnvoyException& e) {
      std::cerr << e.what() << std::endl;
      return ENVOY_FAILURE;
    }

    // Note: We're waiting longer than we might otherwise to drain to the main thread's dispatcher.
    // This is because we're not simply waiting for its availability and for it to have started, but
    // also because we're waiting for clusters to have done their first attempt at DNS resolution.
    // When we improve synchronous failure handling and/or move to dynamic forwarding, we only need
    // to wait until the dispatcher is running (and can drain by enqueueing a drain callback on it,
    // as we did previously).
    postinit_callback_handler_ = main_common_->server()->lifecycleNotifier().registerCallback(
        Envoy::Server::ServerLifecycleNotifier::Stage::PostInit, [this]() -> void {
          server_ = TS_UNCHECKED_READ(main_common_)->server();
          auto api_listener = server_->listenerManager().apiListener()->get().http();
          ASSERT(api_listener.has_value());
          http_dispatcher_->ready(server_->dispatcher(), server_->serverFactoryContext().scope(),
                                  api_listener.value());
        });
  } // mutex_

  // The main run loop must run without holding the mutex, so that the destructor can acquire it.
  bool run_success = TS_UNCHECKED_READ(main_common_)->run();

  // The above call is blocking; at this point the event loop has exited.
  callbacks_.on_exit();

  // Ensure destructors run on Envoy's main thread.
  postinit_callback_handler_.reset();
  TS_UNCHECKED_READ(main_common_).reset();

  return run_success ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

Engine::~Engine() {
  // If we're already on the main thread, it should be safe to simply destruct.
  if (!main_thread_.joinable()) {
    return;
  }

  // If we're not on the main thread, we need to be sure that MainCommon is finished being
  // constructed so we can dispatch shutdown.
  {
    Thread::LockGuard lock(mutex_);
    if (!main_common_) {
      cv_.wait(mutex_);
    }
    ASSERT(main_common_);

    // Exit the event loop and finish up in Engine::run(...)
    event_dispatcher_->exit();
  } // _mutex

  // Now we wait for the main thread to wrap things up.
  main_thread_.join();
}

void Engine::recordCounter(std::string elements, uint64_t count) {
  if (server_) {
    server_->dispatcher().post([this, elements, count]() -> void {
      static const std::string client = "client";
      absl::string_view prefix{client};
      absl::string_view dynamic_elements{elements};
      Stats::Utility::counterFromElements(server_->serverFactoryContext().scope(),
                                          {prefix, dynamic_elements})
          .add(count);
    });
  }
}

void Engine::flushStats() {
  // The server will be null if the post-init callback has not been completed within run().
  // In this case, we can simply ignore the flush.
  if (server_) {
    // Stats must be flushed from the main thread.
    // Dispatching should be moved after https://github.com/lyft/envoy-mobile/issues/720
    server_->dispatcher().post([this]() -> void { server_->flushStats(); });
  }
}

Http::Dispatcher& Engine::httpDispatcher() { return *http_dispatcher_; }

} // namespace Envoy
