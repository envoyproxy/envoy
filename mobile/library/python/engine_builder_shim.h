#pragma once

#include <functional>

#include "library/cc/engine_builder.h"

namespace Envoy {
namespace Python {

// Wraps EngineBuilder::setOnEngineRunning() to acquire the Python GIL
// before invoking the Python callback. Required because C++ callbacks fire
// on Envoy's network thread, not the Python thread.
Platform::EngineBuilder& setOnEngineRunningShim(Platform::EngineBuilder& self,
                                                std::function<void()> closure);

// Wraps EngineBuilder::setOnEngineExit() to acquire the Python GIL
// before invoking the Python callback.
Platform::EngineBuilder& setOnEngineExitShim(Platform::EngineBuilder& self,
                                             std::function<void()> closure);

} // namespace Python
} // namespace Envoy
