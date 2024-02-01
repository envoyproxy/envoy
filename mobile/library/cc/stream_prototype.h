#pragma once

#include <memory>

#include "library/cc/engine.h"
#include "library/cc/envoy_error.h"
#include "library/cc/response_headers.h"
#include "library/cc/response_trailers.h"
#include "library/cc/stream.h"
#include "library/cc/stream_callbacks.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype {
public:
  StreamPrototype(EngineSharedPtr engine);

  StreamSharedPtr start(bool explicit_flow_control = false);

  StreamPrototype& setOnHeaders(OnHeadersCallback closure);
  StreamPrototype& setOnData(OnDataCallback closure);
  StreamPrototype& setOnTrailers(OnTrailersCallback closure);
  StreamPrototype& setOnError(OnErrorCallback closure);
  StreamPrototype& setOnComplete(OnCompleteCallback closure);
  StreamPrototype& setOnCancel(OnCancelCallback closure);
  StreamPrototype& setOnSendWindowAvailable(OnSendWindowAvailableCallback closure);

private:
  EngineSharedPtr engine_;
  StreamCallbacksSharedPtr callbacks_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

} // namespace Platform
} // namespace Envoy
