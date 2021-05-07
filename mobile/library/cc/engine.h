#pragma once

#include <functional>

#include "library/common/types/c_types.h"
#include "log_level.h"
#include "pulse_client.h"
#include "stream_client.h"

namespace Envoy {
namespace Platform {

struct EngineCallbacks {
  std::function<void()> on_engine_running;
  // unused:
  // std::function<void()> on_exit;
};

using EngineCallbacksSharedPtr = std::shared_ptr<EngineCallbacks>;

class Engine {
public:
  ~Engine();

  StreamClientSharedPtr streamClient();
  PulseClientSharedPtr pulseClient();

  void terminate();

private:
  Engine(envoy_engine_t engine, const std::string& configuration, LogLevel log_level);

  friend class EngineBuilder;

  envoy_engine_t engine_;
  EngineCallbacksSharedPtr callbacks_;
  StreamClientSharedPtr stream_client_;
  PulseClientSharedPtr pulse_client_;
  bool terminated_;
};

using EngineSharedPtr = std::shared_ptr<Engine>;

} // namespace Platform
} // namespace Envoy
