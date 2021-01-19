#include "response_headers.h"

namespace Envoy {
namespace Platform {

int ResponseHeaders::http_status() const {
  if (!this->contains(":status")) {
    throw std::logic_error("ResponseHeaders does not contain :status");
  }
  return stoi((*this)[":status"][0]);
}

ResponseHeadersBuilder ResponseHeaders::to_response_headers_builder() {
  ResponseHeadersBuilder builder;
  if (this->contains(":status")) {
    builder.add_http_status(this->http_status());
  }
  for (const auto& pair : this->all_headers()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
