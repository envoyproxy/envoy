#pragma once

#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

class ResponseHeadersRetryHeader : public Upstream::RetryHeader {
public:
  ResponseHeadersRetryHeader() = default;

  bool shouldRetryHeader(const Http::HeaderMap& request_header,
                         const Http::HeaderMap& response_headers) override;
};

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
