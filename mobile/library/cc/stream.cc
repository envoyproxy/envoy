#include "stream.h"

#include <iostream>

#include "bridge_utility.h"
#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Stream::Stream(envoy_stream_t handle, EnvoyHttpCallbacksAdapterSharedPtr adapter)
    : handle_(handle), adapter_(adapter) {}

Stream& Stream::send_headers(RequestHeadersSharedPtr headers, bool end_stream) {
  envoy_headers raw_headers = raw_header_map_as_envoy_headers(headers->all_headers());
  ::send_headers(this->handle_, raw_headers, end_stream);
  return *this;
}

Stream& Stream::send_data(envoy_data data) {
  ::send_data(this->handle_, data, false);
  return *this;
}

void Stream::close(RequestTrailersSharedPtr trailers) {
  envoy_status_t send_trailers(envoy_stream_t stream, envoy_headers trailers);
  envoy_headers raw_headers = raw_header_map_as_envoy_headers(trailers->all_headers());
  ::send_trailers(this->handle_, raw_headers);
}

void Stream::close(envoy_data data) { ::send_data(this->handle_, data, true); }

void Stream::cancel() { reset_stream(this->handle_); }

} // namespace Platform
} // namespace Envoy
