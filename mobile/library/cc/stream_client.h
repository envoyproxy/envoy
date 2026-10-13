#pragma once

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "library/cc/engine.h"
#include "library/cc/stream_prototype.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype;
using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

class StreamClient {
public:
  /**
   * @param engine The underlying engine.
   * @param listener_name The name of the listener this client will route streams to.
   */
  StreamClient(EngineSharedPtr engine, absl::string_view listener_name);

  StreamPrototypeSharedPtr newStreamPrototype();

private:
  EngineSharedPtr engine_;
  std::string listener_name_;
};

using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

} // namespace Platform
} // namespace Envoy
