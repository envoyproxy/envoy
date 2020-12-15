#pragma once

// NOLINT(namespace-envoy)

#include "headers.h"
#include "response_headers_builder.h"

class ResponseHeaders : public Headers {
public:
  int http_status() const;

  ResponseHeadersBuilder to_response_headers_builder();

private:
  ResponseHeaders(RawHeaders headers) : Headers(std::move(headers)) {}

  friend class ResponseHeadersBuilder;
};

using ResponseHeadersSharedPtr = std::shared_ptr<ResponseHeaders>;
