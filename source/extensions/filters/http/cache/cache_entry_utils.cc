#include "source/extensions/filters/http/cache/cache_entry_utils.h"

#include "source/extensions/filters/http/cache/cache_headers_utils.h"

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
  NOT_REACHED_GCOVR_EXCL_LINE;
}

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  return os << cacheEntryStatusString(status);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
