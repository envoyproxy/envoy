#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {

/**
 * Well-known retry policy names.
 */
class RetryPolicyNameValues {
public:
  const std::string ConditionalRetry = "envoy.retry_policy.conditional_retry";
};

using RetryPolicyValues = ConstSingleton<RetryPolicyNameValues>;

} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy
