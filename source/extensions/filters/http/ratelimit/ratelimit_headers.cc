#include "extensions/filters/http/ratelimit/ratelimit_headers.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::ResponseHeaderMapPtr RateLimitHeaders::create(
    Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses) {
  Http::ResponseHeaderMapPtr result = Http::ResponseHeaderMapImpl::create();
  if (!descriptor_statuses || descriptor_statuses->empty()) {
    descriptor_statuses = nullptr;
    return result;
  }

  absl::optional<envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus>
      min_remaining_limit_status;
  std::ostringstream quotaPolicy;
  for (auto&& status : *descriptor_statuses) {
    if (!status.has_current_limit()) {
      continue;
    }
    if (!min_remaining_limit_status ||
        status.limit_remaining() < min_remaining_limit_status.value().limit_remaining()) {
      min_remaining_limit_status.emplace(status);
    }
    uint32_t window = convertRateLimitUnit(status.current_limit().unit());
    if (window) {
      fmt::print(quotaPolicy, ", {:d};{:s}={:d}", status.current_limit().requests_per_unit(),
                 Http::Headers::get().XRateLimitQuotaPolicyKeys.Window, window);
      if (!status.current_limit().name().empty()) {
        fmt::print(quotaPolicy, ";{:s}=\"{:s}\"",
                   Http::Headers::get().XRateLimitQuotaPolicyKeys.Name,
                   status.current_limit().name());
      }
    }
  }

  if (min_remaining_limit_status) {
    std::string rate_limit_limit =
        std::to_string(min_remaining_limit_status.value().current_limit().requests_per_unit()) +
        quotaPolicy.str();
    result->setXRateLimitLimit(rate_limit_limit);
    result->setXRateLimitRemaining(min_remaining_limit_status.value().limit_remaining());
    result->setXRateLimitReset(min_remaining_limit_status.value().seconds_until_reset());
  }
  descriptor_statuses = nullptr;
  return result;
}

uint32_t RateLimitHeaders::convertRateLimitUnit(
    envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit unit) {
  switch (unit) {
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::SECOND:
    return 1;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE:
    return 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR:
    return 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::DAY:
    return 24 * 60 * 60;
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::UNKNOWN:
  default:
    return 0;
  }
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
