#include "request_headers_builder.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Platform {

RequestHeadersBuilder::RequestHeadersBuilder(RequestMethod request_method, std::string scheme,
                                             std::string authority, std::string path) {
  initialize(request_method, std::move(scheme), std::move(authority), std::move(path));
}

RequestHeadersBuilder::RequestHeadersBuilder(RequestMethod request_method, absl::string_view url) {
  Envoy::Http::Utility::Url parsed_url;
  if (!parsed_url.initialize(url, /*is_connect_request=*/false)) {
    initialize(request_method, "", "", "");
    return;
  }
  initialize(request_method, std::string(parsed_url.scheme()),
             std::string(parsed_url.hostAndPort()), std::string(parsed_url.pathAndQueryParams()));
}

void RequestHeadersBuilder::initialize(RequestMethod request_method, std::string scheme,
                                       std::string authority, std::string path) {
  internalSet(":method", {std::string(requestMethodToString(request_method))});
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

RequestHeaders RequestHeadersBuilder::build() const { return RequestHeaders(allHeaders()); }

} // namespace Platform
} // namespace Envoy
