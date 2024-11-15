#include "library/cc/stream_client.h"

namespace Envoy {
namespace Platform {

StreamClient::StreamClient(EngineSharedPtr engine) : engine_(engine) {}

StreamPrototypeSharedPtr StreamClient::newStreamPrototype() {
  return std::make_shared<StreamPrototype>(engine_);
}

} // namespace Platform
} // namespace Envoy
