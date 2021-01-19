#include "request_headers.h"

namespace Envoy {
namespace Platform {

RequestMethod RequestHeaders::request_method() const {
  return request_method_from_string((*this)[":method"][0]);
}

const std::string& RequestHeaders::scheme() const { return (*this)[":scheme"][0]; }

const std::string& RequestHeaders::authority() const { return (*this)[":authority"][0]; }

const std::string& RequestHeaders::path() const { return (*this)[":path"][0]; }

absl::optional<RetryPolicy> RequestHeaders::retry_policy() const {
  try {
    return absl::optional<RetryPolicy>(RetryPolicy::from_raw_header_map(this->all_headers()));
  } catch (std::exception) {
    return absl::optional<RetryPolicy>();
  }
}

absl::optional<UpstreamHttpProtocol> RequestHeaders::upstream_http_protocol() const {
  const auto header_name = "x-envoy-mobile-upstream-protocol";
  if (!this->contains(header_name)) {
    return absl::optional<UpstreamHttpProtocol>();
  }
  return upstream_http_protocol_from_string((*this)[header_name][0]);
}

RequestHeadersBuilder RequestHeaders::to_request_headers_builder() const {
  RequestHeadersBuilder builder(this->request_method(), this->scheme(), this->authority(),
                                this->path());
  for (const auto& pair : this->all_headers()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
