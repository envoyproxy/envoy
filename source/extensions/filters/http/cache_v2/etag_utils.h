#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace EtagUtils {

// Returns true when both values are valid entity tags, neither is weak, and their opaque tags
// match character-for-character, as defined by RFC 9110 section 8.8.3.2:
// https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3.2
bool strongMatch(absl::string_view lhs, absl::string_view rhs);

// Returns true when both values are valid entity tags and their opaque tags match
// character-for-character, regardless of whether either entity tag is weak. See RFC 9110 section
// 8.8.3.2: https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3.2
bool weakMatch(absl::string_view lhs, absl::string_view rhs);

// Returns true when the If-None-Match field value is "*" or contains an entity tag that weakly
// matches etag. An invalid field value does not match.
bool ifNoneMatch(absl::string_view field_value, absl::string_view etag);

} // namespace EtagUtils
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
