#pragma once

#include "library/cc/headers_builder.h"
#include "library/cc/response_headers.h"

namespace Envoy {
namespace Platform {

class ResponseHeaders;
using ResponseHeadersSharedPtr = std::shared_ptr<ResponseHeaders>;

class ResponseHeadersBuilder : public HeadersBuilder {
public:
  ResponseHeadersBuilder() {}

  ResponseHeadersBuilder& addHttpStatus(int status);
  ResponseHeadersSharedPtr build() const;
};

using ResponseHeadersBuilderSharedPtr = std::shared_ptr<ResponseHeadersBuilder>;

} // namespace Platform
} // namespace Envoy
