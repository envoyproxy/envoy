#pragma once

#include <functional>

#include "library/cc/log_level.h"
#include "library/cc/stream_client.h"
#include "library/common/types/c_types.h"

namespace Envoy {
class EngineImpl;
class BaseClientIntegrationTest;

namespace Platform {

class StreamClient;
using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

class Engine : public std::enable_shared_from_this<Engine> {
public:
  ~Engine();

  std::string dumpStats();
  StreamClientSharedPtr streamClient();

  envoy_status_t terminate();
  Envoy::EngineImpl* engine() { return engine_; }

private:
  Engine(::Envoy::EngineImpl* engine);

  // required to access private constructor
  friend class EngineBuilder;
  // required to use envoy_engine_t without exposing it publicly
  friend class StreamPrototype;
  // for testing only
  friend class ::Envoy::BaseClientIntegrationTest;

  Envoy::EngineImpl* engine_;
  StreamClientSharedPtr stream_client_;
};

using EngineSharedPtr = std::shared_ptr<Engine>;

} // namespace Platform
} // namespace Envoy
