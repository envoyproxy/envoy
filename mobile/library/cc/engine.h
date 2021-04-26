#pragma once

#include <functional>

#include "library/common/types/c_types.h"
#include "log_level.h"
#include "pulse_client.h"
#include "stream_client.h"

namespace Envoy {
namespace Platform {

// TODO(crockeo): refactor engine callbacks
//   - make EngineCallbacks struct with on_engine_running and (eventually) on_exit
//   - change context from Engine ptr to EngineCallbacks ptr
//   - move c_on_(...) from private static fn to static fn in anonymous namespace

struct EngineCallbacks {
  std::function<void()> on_engine_running;
  // unused:
  // std::function<void()> on_exit;
};

using EngineCallbacksSharedPtr = std::shared_ptr<EngineCallbacks>;

class Engine {
public:
  ~Engine();

  StreamClientSharedPtr stream_client();
  PulseClientSharedPtr pulse_client();

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
