#include "library/cc/response_headers_builder.h"

namespace Envoy {
namespace Platform {

ResponseHeadersBuilder& ResponseHeadersBuilder::addHttpStatus(int status) {
  internalSet(":status", std::vector<std::string>{std::to_string(status)});
  return *this;
}

ResponseHeadersSharedPtr ResponseHeadersBuilder::build() const {
  ResponseHeaders* headers = new ResponseHeaders(allHeaders());
  return ResponseHeadersSharedPtr(headers);
}

} // namespace Platform
} // namespace Envoy
