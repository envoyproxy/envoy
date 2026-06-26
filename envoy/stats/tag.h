#pragma once

#include <optional>
#include <string>
#include <utility>

#include "envoy/common/optref.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
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
using StatNameTagVectorOptConstRef = std::optional<std::reference_wrapper<const StatNameTagVector>>;

using StatNameTagSpan = absl::Span<const StatNameTag>;
using StatNameTagVec = absl::InlinedVector<StatNameTag, 6>;

// String-view representation of a tag (name, value), used by the string-based scope APIs so
// callers can supply tags without first interning them in the symbol table.
using TagStringView = std::pair<absl::string_view, absl::string_view>;
using TagStringViewSpan = absl::Span<const TagStringView>;

} // namespace Stats
} // namespace Envoy
