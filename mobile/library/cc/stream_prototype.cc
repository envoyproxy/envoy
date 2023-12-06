#include "stream_prototype.h"

#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

StreamPrototype::StreamPrototype(EngineSharedPtr engine) : engine_(engine) {
  callbacks_ = std::make_shared<StreamCallbacks>();
}

StreamSharedPtr StreamPrototype::start(bool explicit_flow_control) {
  auto envoy_stream = init_stream(engine_->engine_);
  start_stream(engine_->engine_, envoy_stream, callbacks_->asEnvoyHttpCallbacks(),
               explicit_flow_control);
  return std::make_shared<Stream>(engine_->engine_, envoy_stream);
}

StreamPrototype& StreamPrototype::setOnHeaders(OnHeadersCallback closure) {
  callbacks_->on_headers = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnData(OnDataCallback closure) {
  callbacks_->on_data = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnTrailers(OnTrailersCallback closure) {
  callbacks_->on_trailers = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnError(OnErrorCallback closure) {
  callbacks_->on_error = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnComplete(OnCompleteCallback closure) {
  callbacks_->on_complete = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnCancel(OnCancelCallback closure) {
  callbacks_->on_cancel = closure;
  return *this;
}

StreamPrototype& StreamPrototype::setOnSendWindowAvailable(OnSendWindowAvailableCallback closure) {
  callbacks_->on_send_window_available = closure;
  return *this;
}

} // namespace Platform
} // namespace Envoy
