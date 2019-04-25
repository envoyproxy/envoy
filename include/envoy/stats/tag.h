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
};

} // namespace Stats
} // namespace Envoy
