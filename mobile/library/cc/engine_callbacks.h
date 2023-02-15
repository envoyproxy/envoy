#pragma once

#include <functional>
#include <memory>

#include "engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

struct EngineCallbacks : public std::enable_shared_from_this<EngineCallbacks> {
  std::function<void()> on_engine_running;
  // unused:
  // std::function<void()> on_exit;

  envoy_engine_callbacks asEnvoyEngineCallbacks();
};

using EngineCallbacksSharedPtr = std::shared_ptr<EngineCallbacks>;

} // namespace Platform
} // namespace Envoy
