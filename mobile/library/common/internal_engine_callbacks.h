#pragma once

#include <functional>

namespace Envoy {

/** The callbacks for the `InternalEngine`. */
struct InternalEngineCallbacks {
  std::function<void()> on_engine_running = [] {};
  std::function<void()> on_exit = [] {};
};

} // namespace Envoy
