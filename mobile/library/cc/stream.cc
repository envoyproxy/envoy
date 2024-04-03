#include "stream.h"

#include "library/cc/bridge_utility.h"
#include "library/common/internal_engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Stream::Stream(Envoy::InternalEngine* engine, envoy_stream_t handle)
    : engine_(engine), handle_(handle) {}

Stream& Stream::sendHeaders(RequestHeadersSharedPtr headers, bool end_stream) {
  engine_->sendHeaders(
      handle_,
      [headers](Http::RequestHeaderMap& request_header_map) {
        Http::StatefulHeaderKeyFormatter& formatter = request_header_map.formatter().value();
        for (const auto& [key, values] : headers->allHeaders()) {
          // Make sure the formatter knows the original case.
          formatter.processKey(key);
          for (const auto& value : values) {
            request_header_map.addCopy(Http::LowerCaseString(key), value);
          }
        }
      },
      end_stream);
  return *this;
}

Stream& Stream::sendData(envoy_data data) {
  engine_->sendData(handle_, data, false);
  return *this;
}

Stream& Stream::readData(size_t bytes_to_read) {
  engine_->readData(handle_, bytes_to_read);
  return *this;
}

void Stream::close(RequestTrailersSharedPtr trailers) {
  envoy_headers raw_headers = rawHeaderMapAsEnvoyHeaders(trailers->allHeaders());
  engine_->sendTrailers(handle_, raw_headers);
}

void Stream::close(envoy_data data) { engine_->sendData(handle_, data, true); }

void Stream::cancel() { engine_->cancelStream(handle_); }

} // namespace Platform
} // namespace Envoy
