#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace RateLimit {

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
};

} // namespace RateLimit
} // namespace Envoy
