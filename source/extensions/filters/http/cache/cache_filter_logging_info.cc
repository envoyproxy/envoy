#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

absl::string_view lookupStatusToString(LookupStatus status) {
  switch (status) {
  case LookupStatus::Unknown:
    return "Unknown";
  case LookupStatus::CacheHit:
    return "CacheHit";
  case LookupStatus::CacheMiss:
    return "CacheMiss";
  case LookupStatus::StaleHitWithSuccessfulValidation:
    return "StaleHitWithSuccessfulValidation";
  case LookupStatus::StaleHitWithFailedValidation:
    return "StaleHitWithFailedValidation";
  case LookupStatus::NotModifiedHit:
    return "NotModifiedHit";
  case LookupStatus::RequestNotCacheable:
    return "RequestNotCacheable";
  case LookupStatus::RequestIncomplete:
    return "RequestIncomplete";
  case LookupStatus::LookupError:
    return "LookupError";
  }
  IS_ENVOY_BUG(absl::StrCat("Unexpected LookupStatus: ", status));
  return "UnexpectedLookupStatus";
}

std::ostream& operator<<(std::ostream& os, const LookupStatus& request_cache_status) {
  return os << lookupStatusToString(request_cache_status);
}

absl::string_view insertStatusToString(InsertStatus status) {
  switch (status) {
  case InsertStatus::InsertSucceeded:
    return "InsertSucceeded";
  case InsertStatus::InsertAbortedByCache:
    return "InsertAbortedByCache";
  case InsertStatus::InsertAbortedCacheCongested:
    return "InsertAbortedCacheCongested";
  case InsertStatus::InsertAbortedResponseIncomplete:
    return "InsertAbortedResponseIncomplete";
  case InsertStatus::HeaderUpdate:
    return "HeaderUpdate";
  case InsertStatus::NoInsertCacheHit:
    return "NoInsertCacheHit";
  case InsertStatus::NoInsertRequestNotCacheable:
    return "NoInsertRequestNotCacheable";
  case InsertStatus::NoInsertResponseNotCacheable:
    return "NoInsertResponseNotCacheable";
  case InsertStatus::NoInsertRequestIncomplete:
    return "NoInsertRequestIncomplete";
  case InsertStatus::NoInsertResponseValidatorsMismatch:
    return "NoInsertResponseValidatorsMismatch";
  case InsertStatus::NoInsertResponseVaryMismatch:
    return "NoInsertResponseVaryMismatch";
  case InsertStatus::NoInsertResponseVaryDisallowed:
    return "NoInsertResponseVaryDisallowed";
  case InsertStatus::NoInsertLookupError:
    return "NoInsertLookupError";
  }
  IS_ENVOY_BUG(absl::StrCat("Unexpected InsertStatus: ", status));
  return "UnexpectedInsertStatus";
}

std::ostream& operator<<(std::ostream& os, const InsertStatus& cache_insert_status) {
  return os << insertStatusToString(cache_insert_status);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
