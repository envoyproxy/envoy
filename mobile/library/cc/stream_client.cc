#include "stream_client.h"

namespace Envoy {
namespace Platform {

StreamClient::StreamClient(envoy_engine_t engine) : engine_(engine) {}

StreamPrototypeSharedPtr StreamClient::new_stream_prototype() {
  return std::make_shared<StreamPrototype>(this->engine_);
}

} // namespace Platform
} // namespace Envoy
