#include "stream.h"

#include "bridge_utility.h"
#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Stream::Stream(envoy_engine_t engine_handle, envoy_stream_t handle)
    : engine_handle_(engine_handle), handle_(handle) {}

Stream& Stream::sendHeaders(RequestHeadersSharedPtr headers, bool end_stream) {
  envoy_headers raw_headers = rawHeaderMapAsEnvoyHeaders(headers->allHeaders());
  ::send_headers(engine_handle_, handle_, raw_headers, end_stream);
  return *this;
}

Stream& Stream::sendData(envoy_data data) {
  ::send_data(engine_handle_, handle_, data, false);
  return *this;
}

Stream& Stream::readData(size_t bytes_to_read) {
  ::read_data(engine_handle_, handle_, bytes_to_read);
  return *this;
}

void Stream::close(RequestTrailersSharedPtr trailers) {
  envoy_headers raw_headers = rawHeaderMapAsEnvoyHeaders(trailers->allHeaders());
  ::send_trailers(engine_handle_, handle_, raw_headers);
}

void Stream::close(envoy_data data) { ::send_data(engine_handle_, handle_, data, true); }

void Stream::cancel() { reset_stream(engine_handle_, handle_); }

} // namespace Platform
} // namespace Envoy
