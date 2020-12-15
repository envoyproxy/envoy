#pragma once

// NOLINT(namespace-envoy)

#include <cstddef>
#include <functional>
#include <memory>

#include "engine.h"
#include "envoy_error.h"
#include "executor.h"
#include "response_headers.h"
#include "response_trailers.h"
#include "stream.h"
#include "stream_callbacks.h"

class Engine;

class StreamPrototype {
public:
  StreamPrototype(Engine engine);

  Stream start(Executor& executor);

  StreamPrototype& set_on_response_headers(OnHeadersCallback closure);
  StreamPrototype& set_on_response_data(OnDataCallback closure);
  StreamPrototype& set_on_response_trailers(OnTrailersCallback closure);
  StreamPrototype& set_on_error(OnErrorCallback closure);
  StreamPrototype& set_on_cancel(OnCancelCallback closure);

private:
  StreamCallbacks callbacks_;
  EnvoyHttpCallbacksAdapter adapter_;
};

using StreamPrototypeSharedPtr = std::shared_ptr<StreamPrototype>;
