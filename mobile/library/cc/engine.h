#pragma once

#include <functional>

#include "library/common/types/c_types.h"
#include "log_level.h"
#include "pulse_client.h"
#include "stream_client.h"

namespace Envoy {
class BaseClientIntegrationTest;

namespace Platform {

class StreamClient;
using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

class Engine : public std::enable_shared_from_this<Engine> {
public:
  StreamClientSharedPtr streamClient();
  PulseClientSharedPtr pulseClient();

  void terminate();

private:
  Engine(envoy_engine_t engine);

  // required to access private constructor
  friend class EngineBuilder;
  // required to use envoy_engine_t without exposing it publicly
  friend class StreamPrototype;
  // for testing only
  friend class ::Envoy::BaseClientIntegrationTest;

  envoy_engine_t engine_;
  StreamClientSharedPtr stream_client_;
  PulseClientSharedPtr pulse_client_;
  bool terminated_;
};

using EngineSharedPtr = std::shared_ptr<Engine>;

} // namespace Platform
} // namespace Envoy
