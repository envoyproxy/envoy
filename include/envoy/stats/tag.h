#pragma once

#include <string>
#include "envoy/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

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

using StatNameTag = std::pair<StatName, StatName>;
using StatNameTagVector = std::vector<StatNameTag>;

} // namespace Stats
} // namespace Envoy
