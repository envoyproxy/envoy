#pragma once

#include <string>
#include <vector>

#include "envoy/ratelimit/ratelimit.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace RateLimit {

inline bool operator==(const RateLimitOverride& lhs, const RateLimitOverride& rhs) {
  return lhs.requests_per_unit_ == rhs.requests_per_unit_ && lhs.unit_ == rhs.unit_;
}

inline bool operator==(const Descriptor& lhs, const Descriptor& rhs) {
  return lhs.entries_ == rhs.entries_ && lhs.limit_ == rhs.limit_;
}

} // namespace RateLimit
} // namespace Envoy
