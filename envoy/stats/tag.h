#pragma once

#include <string>

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class StatName;

/**
 * General representation of a tag.
 */
struct Tag {
  std::string name_;
  std::string value_;

  bool operator==(const Tag& other) const {
    return other.name_ == name_ && other.value_ == value_;
  };
};

using TagVector = std::vector<Tag>;

using StatNameTag = std::pair<StatName, StatName>;
using StatNameTagVector = std::vector<StatNameTag>;
using StatNameTagVectorOptConstRef =
    absl::optional<std::reference_wrapper<const StatNameTagVector>>;

} // namespace Stats
} // namespace Envoy
