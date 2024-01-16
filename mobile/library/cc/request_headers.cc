#include "request_headers.h"

namespace Envoy {
namespace Platform {

RequestMethod RequestHeaders::requestMethod() const {
  return requestMethodFromString((*this)[":method"][0]);
}

const std::string& RequestHeaders::scheme() const { return (*this)[":scheme"][0]; }

const std::string& RequestHeaders::authority() const { return (*this)[":authority"][0]; }

const std::string& RequestHeaders::path() const { return (*this)[":path"][0]; }

absl::optional<RetryPolicy> RequestHeaders::retryPolicy() const {
  return absl::optional<RetryPolicy>(RetryPolicy::fromRawHeaderMap(allHeaders()));
}

RequestHeadersBuilder RequestHeaders::toRequestHeadersBuilder() const {
  RequestHeadersBuilder builder(requestMethod(), scheme(), authority(), path());
  for (const auto& pair : allHeaders()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
