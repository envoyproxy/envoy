#pragma once

#include <string>

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

} // namespace Stats
} // namespace Envoy
