#pragma once

#include <string>
#include <vector>

#include "envoy/ratelimit/ratelimit.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace RateLimit {

inline bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
  return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
}

inline bool operator==(const Descriptor& lhs, const Descriptor& rhs) {
  return lhs.entries_ == rhs.entries_;
}

} // namespace RateLimit
} // namespace Envoy
