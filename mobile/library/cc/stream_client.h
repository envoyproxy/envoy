#pragma once

// NOLINT(namespace-envoy)

#include <memory>

#include "stream_prototype.h"

class StreamClient {
public:
  StreamPrototype new_stream_prototype();
};

using StreamClientSharedPtr = std::shared_ptr<StreamClient>;
