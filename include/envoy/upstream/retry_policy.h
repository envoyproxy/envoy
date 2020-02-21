#pragma once

#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Upstream {

/**
 * A retry policy that speficies policy for retrying upstream requests.
 */
class RetryPolicy {
  // TODO(yxue): interface for retry policy
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
