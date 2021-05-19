#pragma once

#include <memory>

#include "engine.h"
#include "stream_prototype.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype;
using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

class StreamClient {
public:
  StreamClient(EngineSharedPtr engine);

  StreamPrototypeSharedPtr newStreamPrototype();

private:
  EngineSharedPtr engine_;
};

using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

} // namespace Platform
} // namespace Envoy
