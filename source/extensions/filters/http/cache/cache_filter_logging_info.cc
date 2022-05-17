#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

absl::string_view cacheLookupStatusToString(CacheLookupStatus status) {
  switch (status) {
  case CacheLookupStatus::Unknown:
    return "Unknown";
  case CacheLookupStatus::CacheHit:
    return "CacheHit";
  case CacheLookupStatus::CacheMiss:
    return "CacheMiss";
  case CacheLookupStatus::StaleHitWithSuccessfulValidation:
    return "StaleHitWithSuccessfulValidation";
  case CacheLookupStatus::StaleHitWithFailedValidation:
    return "StaleHitWithFailedValidation";
  case CacheLookupStatus::NotModifiedHit:
    return "NotModifiedHit";
  case CacheLookupStatus::RequestNotCacheable:
    return "RequestNotCacheable";
  case CacheLookupStatus::RequestIncomplete:
    return "RequestIncomplete";
  case CacheLookupStatus::LookupError:
    return "LookupError";
  default:
    return "UnrecognizedCacheLookupStatus";
  }
}

std::ostream& operator<<(std::ostream& os, const CacheLookupStatus& request_cache_status) {
  return os << cacheLookupStatusToString(request_cache_status);
}

absl::string_view cacheInsertStatusToString(CacheInsertStatus status) {
  switch (status) {
  case CacheInsertStatus::InsertSucceeded:
    return "InsertSucceeded";
  case CacheInsertStatus::InsertAbortedByCache:
    return "InsertAbortedByCache";
  case CacheInsertStatus::InsertAbortedCacheCongested:
    return "InsertAbortedCacheCongested";
  case CacheInsertStatus::InsertAbortedResponseIncomplete:
    return "InsertAbortedResponseIncomplete";
  case CacheInsertStatus::HeaderUpdate:
    return "HeaderUpdate";
  case CacheInsertStatus::NoInsertCacheHit:
    return "NoInsertCacheHit";
  case CacheInsertStatus::NoInsertRequestNotCacheable:
    return "NoInsertRequestNotCacheable";
  case CacheInsertStatus::NoInsertResponseNotCacheable:
    return "NoInsertResponseNotCacheable";
  case CacheInsertStatus::NoInsertRequestIncomplete:
    return "NoInsertRequestIncomplete";
  case CacheInsertStatus::NoInsertResponseValidatorsMismatch:
    return "NoInsertResponseValidatorsMismatch";
  case CacheInsertStatus::NoInsertResponseVaryMismatch:
    return "NoInsertResponseVaryMismatch";
  case CacheInsertStatus::NoInsertResponseVaryDisallowed:
    return "NoInsertResponseVaryDisallowed";
  case CacheInsertStatus::NoInsertLookupError:
    return "NoInsertLookupError";
  default:
    return "UnrecognizedCacheInsertStatus";
  }
}

std::ostream& operator<<(std::ostream& os, const CacheInsertStatus& cache_insert_status) {
  return os << cacheInsertStatusToString(cache_insert_status);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
