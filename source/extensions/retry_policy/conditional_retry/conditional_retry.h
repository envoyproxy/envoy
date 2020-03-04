#pragma once

#include "envoy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {
class ConditionalRetry : public Router::RetryPolicyExtension {
public:
  ConditionalRetry(const uint32_t retry_on, const std::vector<uint32_t>& retriable_status_codes)
      : retry_on_{retry_on}, retriable_status_codes_{retriable_status_codes} {}

  void recordResponseHeaders(const Http::ResponseHeaderMap& response_header) override {
    would_retry_ = wouldRetryHeaders(response_header);
  }

  void recordReset(Http::StreamResetReason reset_reason) override {
    would_retry_ = wouldRetryReset(reset_reason);
  }

  bool shouldRetry() const override { return would_retry_; }

private:
  bool wouldRetryHeaders(const Http::ResponseHeaderMap& response_header);
  bool wouldRetryReset(Http::StreamResetReason reset_reason);

  const uint32_t retry_on_{};
  const std::vector<uint32_t> retriable_status_codes_;

  bool would_retry_{};
};
} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy