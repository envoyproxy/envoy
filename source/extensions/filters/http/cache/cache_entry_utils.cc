#include "source/extensions/filters/http/cache/cache_entry_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

absl::string_view cacheEntryStatusString(CacheEntryStatus s) {
  switch (s) {
  case CacheEntryStatus::Ok:
    return "Ok";
  case CacheEntryStatus::Unusable:
    return "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return "FoundNotModified";
  }
  PANIC(absl::StrFormat("Unexpected CacheEntryStatus: %d", s));
}

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  return os << cacheEntryStatusString(status);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
