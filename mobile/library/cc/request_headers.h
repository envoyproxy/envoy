#pragma once

#include <optional>

#include "absl/types/optional.h"
#include "library/cc/headers.h"
#include "library/cc/request_headers_builder.h"
#include "library/cc/request_method.h"
#include "library/cc/retry_policy.h"

namespace Envoy {
namespace Platform {

class RequestHeadersBuilder;

class RequestHeaders : public Headers {
public:
  RequestMethod requestMethod() const;
  const std::string& scheme() const;
  const std::string& authority() const;
  const std::string& path() const;
  absl::optional<RetryPolicy> retryPolicy() const;

  RequestHeadersBuilder toRequestHeadersBuilder() const;

private:
  RequestHeaders(RawHeaderMap headers) : Headers(std::move(headers)) {}

  friend class RequestHeadersBuilder;
};

using RequestHeadersSharedPtr = std::shared_ptr<RequestHeaders>;

} // namespace Platform
} // namespace Envoy
