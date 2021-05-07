#pragma once

#include <memory>

#include "stream_prototype.h"

namespace Envoy {
namespace Platform {

class StreamClient {
public:
  StreamClient(envoy_engine_t engine);

  StreamPrototypeSharedPtr newStreamPrototype();

private:
  envoy_engine_t engine_;
};

using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

} // namespace Platform
} // namespace Envoy
