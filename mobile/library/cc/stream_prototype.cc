#include "stream_prototype.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "library/cc/stream.h"
#include "library/common/internal_engine.h"

namespace Envoy {
namespace Platform {

StreamPrototype::StreamPrototype(EngineSharedPtr engine, absl::string_view listener_name)
    : engine_(engine), listener_name_(std::string(listener_name)) {}

StreamSharedPtr StreamPrototype::start(EnvoyStreamCallbacks&& stream_callbacks,
                                       bool explicit_flow_control) {
  auto envoy_stream = engine_->engine()->initStream();
  engine_->engine()->startStream(envoy_stream, std::move(stream_callbacks), explicit_flow_control,
                                 listener_name_);
  return std::make_shared<Stream>(engine_->engine(), envoy_stream);
}

} // namespace Platform
} // namespace Envoy
