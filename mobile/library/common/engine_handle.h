#pragma once

#include "library/common/engine.h"
#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {

/**
 * Wrapper class around the singleton engine handle. This allows us to use C++ access modifiers to
 * control what functionality dependent code is able to access. Furthermore, this allows C++ access
 * to scheduling work on the dispatcher beyond what is available through the public C API.
 */
class EngineHandle {
public:
  /**
   * Execute function on the dispatcher of the provided engine, if this engine is currently running.
   * @param envoy_engine_t, handle to the engine which will be used to execute the function.
   * @param func, function that will be executed if engine exists.
   * @return bool, true if the function was scheduled on the dispatcher.
   */
  static envoy_status_t runOnEngineDispatcher(envoy_engine_t engine,
                                              std::function<void(Envoy::Engine&)> func);

private:
  static std::shared_ptr<Envoy::Engine> engine() {
    // TODO(goaway): enable configurable heap-based allocation
    return engine_.lock();
  }

  static envoy_engine_t initEngine(envoy_engine_callbacks callbacks, envoy_logger logger,
                                   envoy_event_tracker event_tracker);
  static envoy_status_t runEngine(envoy_engine_t, const char* config, const char* log_level);
  static void terminateEngine(envoy_engine_t);

  static EngineSharedPtr strong_engine_;
  static EngineWeakPtr engine_;

  // Allow a specific list of functions to access the internal setup/teardown functionality.
  friend envoy_engine_t(::init_engine)(envoy_engine_callbacks callbacks, envoy_logger logger,
                                       envoy_event_tracker event_tracker);
  friend envoy_status_t(::run_engine)(envoy_engine_t, const char* config, const char* log_level);
  friend void ::terminate_engine(envoy_engine_t engine);
};

} // namespace Envoy
