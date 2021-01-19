#include "response_headers_builder.h"

namespace Envoy {
namespace Platform {

ResponseHeadersBuilder& ResponseHeadersBuilder::add_http_status(int status) {
  this->internal_set(":status", std::vector<std::string>{std::to_string(status)});
  return *this;
}

ResponseHeadersSharedPtr ResponseHeadersBuilder::build() const {
  ResponseHeaders* headers = new ResponseHeaders(this->all_headers());
  return std::shared_ptr<ResponseHeaders>(headers);
}

} // namespace Platform
} // namespace Envoy
