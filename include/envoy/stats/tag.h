#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * General representation of a tag.
 */
struct Tag {
  absl::string_view name_;
  absl::string_view value_;
};

} // namespace Stats
} // namespace Envoy
