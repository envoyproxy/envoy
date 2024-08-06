#include "stream.h"

#include "library/common/bridge//utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Stream::Stream(InternalEngine* engine, envoy_stream_t handle) : engine_(engine), handle_(handle) {}

Stream& Stream::sendHeaders(Http::RequestHeaderMapPtr headers, bool end_stream, bool idempotent) {
  engine_->sendHeaders(handle_, std::move(headers), end_stream, idempotent);
  return *this;
}

Stream& Stream::sendData(Buffer::InstancePtr buffer) {
  engine_->sendData(handle_, std::move(buffer), false);
  return *this;
}

Stream& Stream::readData(size_t bytes_to_read) {
  engine_->readData(handle_, bytes_to_read);
  return *this;
}

void Stream::close(Http::RequestTrailerMapPtr trailers) {
  engine_->sendTrailers(handle_, std::move(trailers));
}

void Stream::close(Buffer::InstancePtr buffer) {
  engine_->sendData(handle_, std::move(buffer), true);
}

void Stream::cancel() { engine_->cancelStream(handle_); }

} // namespace Platform
} // namespace Envoy
