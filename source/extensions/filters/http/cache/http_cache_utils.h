#pragma once

#include "envoy/common/time.h"
#include "envoy/http/header_map.h"

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
class Utils {
public:
  // Parses and returns max-age or s-maxage (with s-maxage taking precedence),
  // parsed into a SystemTime::Duration. Returns SystemTime::Duration::zero if
  // neither is present, or there is a no-cache directive, or if max-age or
  // s-maxage is malformed.
  static SystemTime::duration effectiveMaxAge(absl::string_view cache_control);

  // Parses header_entry as an HTTP time. Returns SystemTime() if
  // header_entry is null or malformed.
  static SystemTime httpTime(const Http::HeaderEntry* header_entry);

private:
  static bool tchar(char c);
  static bool eatToken(absl::string_view& s);
  static void eatDirectiveArgument(absl::string_view& s);
  static SystemTime::duration eatLeadingDuration(absl::string_view& s);
};
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
