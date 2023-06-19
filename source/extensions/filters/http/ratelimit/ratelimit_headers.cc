#include "source/extensions/filters/http/ratelimit/ratelimit_headers.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/common/ratelimit_headers.h"

#include "absl/strings/substitute.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::ResponseHeaderMapPtr XRateLimitHeaderUtils::create(
    Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses) {
  Http::ResponseHeaderMapPtr result = Http::ResponseHeaderMapImpl::create();
  if (!descriptor_statuses || descriptor_statuses->empty()) {
    descriptor_statuses = nullptr;
    return result;
  }

  absl::optional<envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus>
      min_remaining_limit_status;
  std::string quota_policy;
  for (auto&& status : *descriptor_statuses) {
    if (!status.has_current_limit()) {
      continue;
    }
    if (!min_remaining_limit_status ||
        status.limit_remaining() < min_remaining_limit_status.value().limit_remaining()) {
      min_remaining_limit_status.emplace(status);
    }
    const uint32_t window = convertRateLimitUnit(status.current_limit().unit());
    // Constructing the quota-policy per RFC
    // https://tools.ietf.org/id/draft-polli-ratelimit-headers-02.html#name-ratelimit-limit
    // Example of the result: `, 10;w=1;name="per-ip", 1000;w=3600`
    if (window) {
      // For each descriptor status append `<LIMIT>;w=<WINDOW_IN_SECONDS>`
      absl::SubstituteAndAppend(
          &quota_policy, ", $0;$1=$2", status.current_limit().requests_per_unit(),
          HttpFilters::Common::RateLimit::XRateLimitHeaders::get().QuotaPolicyKeys.Window, window);
      if (!status.current_limit().name().empty()) {
        // If the descriptor has a name, append `;name="<DESCRIPTOR_NAME>"`
        absl::SubstituteAndAppend(
            &quota_policy, ";$0=\"$1\"",
            HttpFilters::Common::RateLimit::XRateLimitHeaders::get().QuotaPolicyKeys.Name,
            status.current_limit().name());
      }
    }
  }

  if (min_remaining_limit_status) {
    const std::string rate_limit_limit = absl::StrCat(
        min_remaining_limit_status.value().current_limit().requests_per_unit(), quota_policy);
    result->addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit, rate_limit_limit);
    result->addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining,
        min_remaining_limit_status.value().limit_remaining());
    result->addReferenceKey(
        HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset,
        min_remaining_limit_status.value().duration_until_reset().seconds());
  }
  descriptor_statuses = nullptr;
  return result;
}

uint32_t XRateLimitHeaderUtils::convertRateLimitUnit(
    const envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit unit) {
  switch (unit) {
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::SECOND:
    return 1;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE:
    return 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR:
    return 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::DAY:
    return 24 * 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MONTH:
    return 30 * 24 * 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::YEAR:
    return 365 * 24 * 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::UNKNOWN:
  default:
    return 0;
  }
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
