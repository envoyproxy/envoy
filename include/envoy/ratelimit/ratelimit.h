#pragma once

#include <string>
#include <vector>

#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "absl/time/time.h"
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

  friend bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
    return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
  }
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct Descriptor {
  std::vector<DescriptorEntry> entries_;
  absl::optional<RateLimitOverride> limit_ = absl::nullopt;
};

/**
 * A single token bucket. See token_bucket.proto.
 */
struct TokenBucket {
  uint32_t max_tokens_;
  uint32_t tokens_per_fill_;
  absl::Duration fill_interval_;

  friend bool operator==(const TokenBucket& lhs, const TokenBucket& rhs) {
    return lhs.max_tokens_ == rhs.max_tokens_ && lhs.tokens_per_fill_ == rhs.tokens_per_fill_ &&
           lhs.fill_interval_ == rhs.fill_interval_;
  }

  // Support absl::Hash.
  template <typename H>
  friend H AbslHashValue(H h, const TokenBucket& d) { // NOLINT(readability-identifier-naming)
    h = H::combine(std::move(h), d.max_tokens_, d.tokens_per_fill_, d.fill_interval_);
    return h;
  }
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct LocalDescriptor {
  std::vector<DescriptorEntry> entries_;
  TokenBucket token_bucket_;

  friend bool operator==(const LocalDescriptor& lhs, const LocalDescriptor& rhs) {
    return lhs.entries_ == rhs.entries_ && lhs.token_bucket_ == rhs.token_bucket_;
  }

  // Support absl::Hash.
  template <typename H>
  friend H AbslHashValue(H h, const LocalDescriptor& d) { // NOLINT(readability-identifier-naming)
    for (const auto& entry : d.entries_) {
      h = H::combine(std::move(h), entry.key_, entry.value_);
    }
    h = H::combine(std::move(h), d.token_bucket_);
    return h;
  }
};

} // namespace RateLimit
} // namespace Envoy
