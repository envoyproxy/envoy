#include "source/extensions/filters/http/cache/cache_entry_utils.h"

#include "source/extensions/filters/http/cache/cache_headers_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

std::string CacheEntryStatusString(CacheEntryStatus s) {
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

inline std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Ok:
    return os << "Ok";
  case CacheEntryStatus::Unusable:
    return os << "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return os << "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return os << "FoundNotModified";
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
