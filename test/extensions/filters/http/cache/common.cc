#include "test/extensions/filters/http/cache/common.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
  std::vector<std::string> fields;

  if (request_cache_control.must_validate_) {
    fields.push_back("must_validate");
  }
  if (request_cache_control.no_store_) {
    fields.push_back("no_store");
  }
  if (request_cache_control.no_transform_) {
    fields.push_back("no_transform");
  }
  if (request_cache_control.only_if_cached_) {
    fields.push_back("only_if_cached");
  }
  if (request_cache_control.max_age_.has_value()) {
    fields.push_back(
        absl::StrCat("max-age=", std::to_string(request_cache_control.max_age_->count())));
  }
  if (request_cache_control.min_fresh_.has_value()) {
    fields.push_back(
        absl::StrCat("min-fresh=", std::to_string(request_cache_control.min_fresh_->count())));
  }
  if (request_cache_control.max_stale_.has_value()) {
    fields.push_back(
        absl::StrCat("max-stale=", std::to_string(request_cache_control.max_stale_->count())));
  }

  return os << "{" << absl::StrJoin(fields, ", ") << "}";
}

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  std::vector<std::string> fields;

  if (response_cache_control.must_validate_) {
    fields.push_back("must_validate");
  }
  if (response_cache_control.no_store_) {
    fields.push_back("no_store");
  }
  if (response_cache_control.no_transform_) {
    fields.push_back("no_transform");
  }
  if (response_cache_control.no_stale_) {
    fields.push_back("no_stale");
  }
  if (response_cache_control.is_public_) {
    fields.push_back("public");
  }
  if (response_cache_control.max_age_.has_value()) {
    fields.push_back(
        absl::StrCat("max-age=", std::to_string(response_cache_control.max_age_->count())));
  }

  return os << "{" << absl::StrJoin(fields, ", ") << "}";
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
