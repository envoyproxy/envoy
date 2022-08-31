#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

/**
 * A client used to query a rate limit quota service (RLQS).
 */
class RateLimitClient {
public:
  virtual ~RateLimitClient() = default;
  // TODO(tyxia) How to define this interface call
  virtual void rateLimit() PURE;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
