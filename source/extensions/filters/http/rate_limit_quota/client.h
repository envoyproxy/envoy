#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;

/**
 * Async callbacks used during rateLimit() calls.
 */
class RateLimitQuotaCallbacks {
public:
  virtual ~RateLimitQuotaCallbacks() = default;

  virtual void
  onQuotaResponse(envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse& response) PURE;
};

/**
 * A client used to query a rate limit quota service (RLQS).
 */
class RateLimitClient {
public:
  virtual ~RateLimitClient() = default;

  virtual void rateLimit(RateLimitQuotaCallbacks& callbacks) PURE;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
