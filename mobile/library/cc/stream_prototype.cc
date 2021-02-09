#include "stream_prototype.h"

#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

StreamPrototype::StreamPrototype(envoy_engine_t engine) : engine_(engine) {
  this->callbacks_ = std::make_shared<StreamCallbacks>();
}

StreamSharedPtr StreamPrototype::start() {
  auto stream = init_stream(this->engine_);
  start_stream(stream, this->callbacks_->as_envoy_http_callbacks());

  return std::make_shared<Stream>(stream, this->callbacks_);
}

StreamPrototype& StreamPrototype::set_on_headers(OnHeadersCallback closure) {
  this->callbacks_->on_headers = closure;
  return *this;
}

StreamPrototype& StreamPrototype::set_on_data(OnDataCallback closure) {
  this->callbacks_->on_data = closure;
  return *this;
}

StreamPrototype& StreamPrototype::set_on_trailers(OnTrailersCallback closure) {
  this->callbacks_->on_trailers = closure;
  return *this;
}

StreamPrototype& StreamPrototype::set_on_error(OnErrorCallback closure) {
  this->callbacks_->on_error = closure;
  return *this;
}

StreamPrototype& StreamPrototype::set_on_complete(OnCompleteCallback closure) {
  this->callbacks_->on_complete = closure;
  return *this;
}

StreamPrototype& StreamPrototype::set_on_cancel(OnCancelCallback closure) {
  this->callbacks_->on_cancel = closure;
  return *this;
}

} // namespace Platform
} // namespace Envoy
