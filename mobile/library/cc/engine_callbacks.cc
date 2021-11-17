#include "engine_callbacks.h"

namespace Envoy {
namespace Platform {

namespace {

void c_on_engine_running(void* context) {
  auto engine_callbacks = *static_cast<EngineCallbacksSharedPtr*>(context);
  engine_callbacks->on_engine_running();
}

void c_on_exit(void* context) {
  auto engine_callbacks_ptr = static_cast<EngineCallbacksSharedPtr*>(context);
  delete engine_callbacks_ptr;
}

} // namespace

envoy_engine_callbacks EngineCallbacks::asEnvoyEngineCallbacks() {
  return envoy_engine_callbacks{
      &c_on_engine_running,
      &c_on_exit,
      new EngineCallbacksSharedPtr(shared_from_this()),
  };
}

} // namespace Platform
} // namespace Envoy
