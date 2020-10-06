#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

/**
 * A local rate limiter.
 */
class LocalRateLimiter {
public:
  virtual ~LocalRateLimiter() = default;

  /**
   * Checks if a request is allowed.
   */
  virtual bool requestAllowed() const PURE;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
