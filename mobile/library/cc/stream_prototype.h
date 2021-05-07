#pragma once

#include <memory>

#include "envoy_error.h"
#include "library/common/types/c_types.h"
#include "response_headers.h"
#include "response_trailers.h"
#include "stream.h"
#include "stream_callbacks.h"

namespace Envoy {
namespace Platform {

class StreamPrototype {
public:
  StreamPrototype(envoy_engine_t engine);

  StreamSharedPtr start();

  StreamPrototype& setOnHeaders(OnHeadersCallback closure);
  StreamPrototype& setOnData(OnDataCallback closure);
  StreamPrototype& setOnTrailers(OnTrailersCallback closure);
  StreamPrototype& setOnError(OnErrorCallback closure);
  StreamPrototype& setOnComplete(OnCompleteCallback closure);
  StreamPrototype& setOnCancel(OnCancelCallback closure);

private:
  envoy_engine_t engine_;
  StreamCallbacksSharedPtr callbacks_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

} // namespace Platform
} // namespace Envoy
