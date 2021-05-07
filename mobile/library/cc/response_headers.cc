#include "response_headers.h"

namespace Envoy {
namespace Platform {

int ResponseHeaders::httpStatus() const {
  if (!this->contains(":status")) {
    throw std::logic_error("ResponseHeaders does not contain :status");
  }
  return stoi((*this)[":status"][0]);
}

ResponseHeadersBuilder ResponseHeaders::toResponseHeadersBuilder() {
  ResponseHeadersBuilder builder;
  if (this->contains(":status")) {
    builder.addHttpStatus(this->httpStatus());
  }
  for (const auto& pair : this->allHeaders()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
