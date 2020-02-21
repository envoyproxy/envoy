#pragma once

#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Upstream {

/**
 * A retry policy that specifies policy for retrying upstream requests.
 */
class RetryPolicy {
public:
  virtual ~RetryPolicy() = default;

  /**
   * Called when an upstream request has been completed with headers.
   *
   * @param response_header response header.
   */
  virtual void recordResponseHeaders(const Http::HeaderMap& response_header) PURE;

  /**
   * Called when an upstream request failed due to a reset.
   *
   * @param reset_reason reset reason.
   */
  virtual void recordReset(Http::StreamResetReason reset_reason) PURE;

  /**
   * Determine if the request should be retried. The plugin can make the decision based on the
   * request and reset records.
   * @return a boolean value indicating if the request should be retried.
   */
  virtual bool shouldRetry() const PURE;
};

using RetryPolicySharedPtr = std::shared_ptr<RetryPolicy>;

/**
 * Factory for RetryPolicy
 */
class RetryPolicyFactory : public Config::TypedFactory {
public:
  virtual ~RetryPolicyFactory() = default;

  virtual RetryPolicySharedPtr createRetryPolicy(const Protobuf::Message& config,
                                                 const Http::HeaderMap& request_header) PURE;

  std::string category() const override { return "envoy.retry_policy"; }
};

} // namespace Upstream
} // namespace Envoy
