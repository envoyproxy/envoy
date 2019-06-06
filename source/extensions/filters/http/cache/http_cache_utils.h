#pragma once

#include "envoy/http/header_map.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace Internal {
// Parses and returns max-age or s-maxage (with s-maxage taking precedence),
// parsed into a Duration. Returns -InfiniteDuration if neither is present, or
// there is a no-cache directive, or if max-age or s-maxage is malformed.
absl::Duration effectiveMaxAge(absl::string_view cache_control);

// Parses header_entry as an HTTP time. Returns InifinitePast() if header_entry
// is null or malformed.
absl::Time httpTime(const Http::HeaderEntry* header_entry);
} // namespace Internal
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
