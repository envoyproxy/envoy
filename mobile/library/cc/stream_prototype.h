#pragma once

#include <memory>

#include "engine.h"
#include "envoy_error.h"
#include "library/common/types/c_types.h"
#include "response_headers.h"
#include "response_trailers.h"
#include "stream.h"
#include "stream_callbacks.h"

namespace Envoy {
namespace Platform {

class Engine;
using EngineSharedPtr = std::shared_ptr<Engine>;

class StreamPrototype {
public:
  StreamPrototype(EngineSharedPtr engine);

  StreamSharedPtr start();

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
