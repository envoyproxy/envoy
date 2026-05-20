#pragma once

#include <string>

#include "envoy/common/optref.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

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

using StatNameTagSpan = absl::Span<const StatNameTag>;
using StatNameTagVec = absl::InlinedVector<StatNameTag, 6>;

} // namespace Stats
} // namespace Envoy
