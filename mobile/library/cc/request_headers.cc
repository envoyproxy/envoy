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
  try {
    return absl::optional<RetryPolicy>(RetryPolicy::fromRawHeaderMap(this->allHeaders()));
  } catch (const std::exception&) {
    return absl::optional<RetryPolicy>();
  }
}

absl::optional<UpstreamHttpProtocol> RequestHeaders::upstreamHttpProtocol() const {
  const auto header_name = "x-envoy-mobile-upstream-protocol";
  if (!this->contains(header_name)) {
    return absl::optional<UpstreamHttpProtocol>();
  }
  return upstreamHttpProtocolFromString((*this)[header_name][0]);
}

RequestHeadersBuilder RequestHeaders::toRequestHeadersBuilder() const {
  RequestHeadersBuilder builder(this->requestMethod(), this->scheme(), this->authority(),
                                this->path());
  for (const auto& pair : this->allHeaders()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
