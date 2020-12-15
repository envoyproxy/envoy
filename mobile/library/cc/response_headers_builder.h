#pragma once

// NOLINT(namespace-envoy)

#include "headers_builder.h"
#include "response_headers.h"

class ResponseHeaders;

class ResponseHeadersBuilder : public HeadersBuilder {
public:
  ResponseHeadersBuilder() {}

  ResponseHeadersBuilder& add_http_status(int status);
  ResponseHeaders build() const;
};

using ResponseHeadersBuilderSharedPtr = std::shared_ptr<ResponseHeadersBuilder>;
