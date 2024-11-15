#include "stream_prototype.h"

#include "library/common/internal_engine.h"

namespace Envoy {
namespace Platform {

StreamPrototype::StreamPrototype(EngineSharedPtr engine) : engine_(engine) {}

StreamSharedPtr StreamPrototype::start(EnvoyStreamCallbacks&& stream_callbacks,
                                       bool explicit_flow_control) {
  auto envoy_stream = engine_->engine_->initStream();
  engine_->engine_->startStream(envoy_stream, std::move(stream_callbacks), explicit_flow_control);
  return std::make_shared<Stream>(engine_->engine_, envoy_stream);
}

} // namespace Platform
} // namespace Envoy
