#pragma once

// NOLINT(namespace-envoy)

#include <string>

#include "headers_builder.h"
#include "request_headers.h"
#include "request_method.h"
#include "retry_policy.h"
#include "upstream_http_protocol.h"

class RequestHeaders;

class RequestHeadersBuilder : public HeadersBuilder {
public:
  RequestHeadersBuilder(RequestMethod request_method, const std::string& scheme,
                        const std::string& authority, const std::string& path);

  RequestHeadersBuilder& add_retry_policy(const RetryPolicy& retry_policy);
  RequestHeadersBuilder& add_upstream_http_protocol(UpstreamHttpProtocol upstream_http_protocol);

  RequestHeaders build() const;
};

using RequestHeadersBuilderSharedPtr = std::shared_ptr<RequestHeadersBuilder>;
