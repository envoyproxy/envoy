#pragma once

#include <functional>

#include "library/common/types/c_types.h"
#include "log_level.h"
#include "pulse_client.h"
#include "stream_client.h"

namespace Envoy {
namespace Platform {

class Engine {
public:
  ~Engine();

  StreamClientSharedPtr stream_client();
  PulseClientSharedPtr pulse_client();

private:
  Engine(envoy_engine_t engine, const std::string& configuration, LogLevel log_level,
         std::function<void()> on_engine_running);

  static void c_on_engine_running(void* context);
  static void c_on_exit(void* context);

  friend class EngineBuilder;

  envoy_engine_t engine_;
  std::function<void()> on_engine_running_;
  StreamClientSharedPtr stream_client_;
  PulseClientSharedPtr pulse_client_;
};

using EngineSharedPtr = std::shared_ptr<Engine>;

} // namespace Platform
} // namespace Envoy
