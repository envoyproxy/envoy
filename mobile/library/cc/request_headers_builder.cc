#include "request_headers_builder.h"

namespace Envoy {
namespace Platform {

RequestHeadersBuilder::RequestHeadersBuilder(RequestMethod request_method, std::string scheme,
                                             std::string authority, std::string path) {
  this->internalSet(":method", {requestMethodToString(request_method)});
  this->internalSet(":scheme", {std::move(scheme)});
  this->internalSet(":authority", {std::move(authority)});
  this->internalSet(":path", {std::move(path)});
}

RequestHeadersBuilder& RequestHeadersBuilder::addRetryPolicy(const RetryPolicy& retry_policy) {
  const RawHeaderMap retry_policy_headers = retry_policy.asRawHeaderMap();
  for (const auto& pair : retry_policy_headers) {
    this->internalSet(pair.first, pair.second);
  }
  return *this;
}

RequestHeadersBuilder&
RequestHeadersBuilder::addUpstreamHttpProtocol(UpstreamHttpProtocol upstream_http_protocol) {
  this->internalSet("x-envoy-mobile-upstream-protocol",
                    std::vector<std::string>{upstreamHttpProtocolToString(upstream_http_protocol)});
  return *this;
}

RequestHeaders RequestHeadersBuilder::build() const { return RequestHeaders(this->allHeaders()); }

} // namespace Platform
} // namespace Envoy
