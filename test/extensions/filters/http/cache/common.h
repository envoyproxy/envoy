#pragma once

#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
  std::string s = "{";
  s += request_cache_control.must_validate_ ? "must_validate, " : "";
  s += request_cache_control.no_store_ ? "no_store, " : "";
  s += request_cache_control.no_transform_ ? "no_transform, " : "";
  s += request_cache_control.only_if_cached_ ? "only_if_cached, " : "";

  s += request_cache_control.max_age_.has_value()
           ? "max-age=" + std::to_string(request_cache_control.max_age_.value().count()) + ", "
           : "";
  s += request_cache_control.min_fresh_.has_value()
           ? "min-fresh=" + std::to_string(request_cache_control.min_fresh_.value().count()) + ", "
           : "";
  s += request_cache_control.max_stale_.has_value()
           ? "max-stale=" + std::to_string(request_cache_control.max_stale_.value().count()) + ", "
           : "";

  // Remove any extra ", " at the end
  if (s.size() > 1) {
    s.pop_back();
    s.pop_back();
  }

  s += "}";
  return os << s;
}

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  std::string s = "{";
  s += response_cache_control.must_validate_ ? "must_validate, " : "";
  s += response_cache_control.no_store_ ? "no_store, " : "";
  s += response_cache_control.no_transform_ ? "no_transform, " : "";
  s += response_cache_control.no_stale_ ? "no_stale, " : "";
  s += response_cache_control.is_public_ ? "public, " : "";

  s += response_cache_control.max_age_.has_value()
           ? "max-age=" + std::to_string(response_cache_control.max_age_.value().count()) + ", "
           : "";

  // Remove any extra ", " at the end
  if (s.size() > 1) {
    s.pop_back();
    s.pop_back();
  }

  s += "}";
  return os << s;
}

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Ok:
    return os << "Ok";
  case CacheEntryStatus::Unusable:
    return os << "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return os << "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return os << "FoundNotModified";
  case CacheEntryStatus::SatisfiableRange:
    return os << "SatisfiableRange";
  case CacheEntryStatus::NotSatisfiableRange:
    return os << "NotSatisfiableRange";
  }
  PANIC("reached unexpected code");
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
