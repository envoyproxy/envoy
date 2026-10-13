#include "library/cc/stream_client.h"

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Platform {

StreamClient::StreamClient(EngineSharedPtr engine, absl::string_view listener_name)
    : engine_(engine), listener_name_(std::string(listener_name)) {}

StreamPrototypeSharedPtr StreamClient::newStreamPrototype() {
  return std::make_shared<StreamPrototype>(engine_, listener_name_);
}

} // namespace Platform
} // namespace Envoy
