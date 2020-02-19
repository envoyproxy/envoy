#pragma once

#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

class ResponseHeadersRetryHeader : public Upstream::RetryHeader {
public:
  ResponseHeadersRetryHeader(const uint32_t retry_on,
                             const std::vector<uint32_t>& retriable_status_codes,
                             const std::vector<Http::HeaderMatcherSharedPtr>& retriable_headers)
      : retry_on_(retry_on), retriable_status_codes_(retriable_status_codes),
        retriable_headers_(retriable_headers) {}

  bool shouldRetryHeader(const Http::HeaderMap& request_header,
                         const Http::HeaderMap& response_headers) override;

private:
  const uint32_t retry_on_;
  const std::vector<uint32_t> retriable_status_codes_;
  const std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
};

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
