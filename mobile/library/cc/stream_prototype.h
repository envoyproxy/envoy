#pragma once

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "library/cc/engine.h"
#include "library/cc/stream.h"
#include "library/common/engine_types.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype {
public:
  /**
   * @param engine The underlying engine.
   * @param listener_name The name of the listener this stream will be started on.
   */
  StreamPrototype(EngineSharedPtr engine, absl::string_view listener_name);

  /** Starts the stream. */
  StreamSharedPtr start(EnvoyStreamCallbacks&& stream_callbacks,
                        bool explicit_flow_control = false);

private:
  EngineSharedPtr engine_;
  std::string listener_name_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

} // namespace Platform
} // namespace Envoy
