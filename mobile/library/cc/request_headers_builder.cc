#include "request_headers_builder.h"

namespace Envoy {
namespace Platform {

RequestHeadersBuilder::RequestHeadersBuilder(RequestMethod request_method, std::string scheme,
                                             std::string authority, std::string path) {
  internalSet(":method", {requestMethodToString(request_method)});
  internalSet(":scheme", {std::move(scheme)});
  internalSet(":authority", {std::move(authority)});
  internalSet(":path", {std::move(path)});
}

RequestHeadersBuilder& RequestHeadersBuilder::addRetryPolicy(const RetryPolicy& retry_policy) {
  const RawHeaderMap retry_policy_headers = retry_policy.asRawHeaderMap();
  for (const auto& pair : retry_policy_headers) {
    internalSet(pair.first, pair.second);
  }
  return *this;
}

RequestHeadersBuilder&
RequestHeadersBuilder::addUpstreamHttpProtocol(UpstreamHttpProtocol upstream_http_protocol) {
  internalSet("x-envoy-mobile-upstream-protocol",
              std::vector<std::string>{upstreamHttpProtocolToString(upstream_http_protocol)});
  return *this;
}

RequestHeaders RequestHeadersBuilder::build() const { return RequestHeaders(allHeaders()); }

} // namespace Platform
} // namespace Envoy
