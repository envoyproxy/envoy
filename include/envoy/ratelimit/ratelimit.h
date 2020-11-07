#pragma once

#include <string>
#include <vector>

#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace RateLimit {

/**
 * An optional dynamic override for the rate limit. See ratelimit.proto
 */
struct RateLimitOverride {
  uint32_t requests_per_unit_;
  envoy::type::v3::RateLimitUnit unit_;
};

/**
 * A single rate limit request descriptor entry. See ratelimit.proto.
 */
struct DescriptorEntry {
  std::string key_;
  std::string value_;
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct Descriptor {
  std::vector<DescriptorEntry> entries_;
  absl::optional<RateLimitOverride> limit_ = absl::nullopt;
};

} // namespace RateLimit
} // namespace Envoy
