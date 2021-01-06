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

  StreamPrototype& set_on_headers(OnHeadersCallback closure);
  StreamPrototype& set_on_data(OnDataCallback closure);
  StreamPrototype& set_on_trailers(OnTrailersCallback closure);
  StreamPrototype& set_on_error(OnErrorCallback closure);
  StreamPrototype& set_on_complete(OnCompleteCallback closure);
  StreamPrototype& set_on_cancel(OnCancelCallback closure);

private:
  envoy_engine_t engine_;
  StreamCallbacksSharedPtr callbacks_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;

} // namespace Platform
} // namespace Envoy
