#include "library/common/engine_handle.h"

namespace Envoy {

envoy_status_t EngineHandle::runOnEngineDispatcher(envoy_engine_t,
                                                   std::function<void(Envoy::Engine&)> func) {
  if (auto e = engine()) {
    return e->dispatcher().post([func]() {
      if (auto e = engine()) {
        func(*e);
      }
    });
  }
  return ENVOY_FAILURE;
}

envoy_engine_t EngineHandle::initEngine(envoy_engine_callbacks callbacks, envoy_logger logger,
                                        envoy_event_tracker event_tracker) {
  // TODO(goaway): return new handle once multiple engine support is in place.
  // https://github.com/envoyproxy/envoy-mobile/issues/332
  strong_engine_ = std::make_shared<Envoy::Engine>(callbacks, logger, event_tracker);
  engine_ = strong_engine_;
  return 1;
}

envoy_status_t EngineHandle::runEngine(envoy_engine_t, const char* config, const char* log_level) {
  // This will change once multiple engine support is in place.
  // https://github.com/envoyproxy/envoy-mobile/issues/332
  if (auto e = engine()) {
    e->run(config, log_level);
    return ENVOY_SUCCESS;
  }

  return ENVOY_FAILURE;
}

void EngineHandle::terminateEngine(envoy_engine_t) {
  // Reset the primary handle to the engine, but retain it long enough to synchronously terminate.
  auto e = strong_engine_;
  strong_engine_.reset();
  e->terminate();
}

EngineSharedPtr EngineHandle::strong_engine_;
EngineWeakPtr EngineHandle::engine_;

} // namespace Envoy
