#pragma once

#include <memory>

#include "library/cc/engine.h"
#include "library/cc/stream.h"
#include "library/common/engine_types.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype {
public:
  explicit StreamPrototype(EngineSharedPtr engine);

  /** Starts the stream. */
  StreamSharedPtr start(EnvoyStreamCallbacks&& stream_callbacks,
                        bool explicit_flow_control = false);

private:
  EngineSharedPtr engine_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

} // namespace Platform
} // namespace Envoy
