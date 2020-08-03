#include "extensions/filters/http/ratelimit/ratelimit_headers.h"

#include "common/http/header_map_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    x_rate_limit_limit_handle(Http::CustomHeaders::get().XRateLimitLimit);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    x_rate_limit_remaining_handle(Http::CustomHeaders::get().XRateLimitRemaining);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    x_rate_limit_reset_handle(Http::CustomHeaders::get().XRateLimitReset);

Http::ResponseHeaderMapPtr RateLimitHeaders::create(
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
    uint32_t window = convertRateLimitUnit(status.current_limit().unit());
    // Constructing the quota-policy per RFC
    // https://tools.ietf.org/id/draft-polli-ratelimit-headers-00.html#rfc.section.3.1
    // Example of the result: `, 10;window=1;name="per-ip", 1000;window=3600`
    if (window) {
      // For each descriptor status append `<LIMIT>;window=<WINDOW_IN_SECONDS>`
      absl::StrAppend(&quota_policy,
                      fmt::format(", {:d};{:s}={:d}", status.current_limit().requests_per_unit(),
                                  Http::CustomHeaders::get().XRateLimitQuotaPolicyKeys.Window,
                                  window));
      if (!status.current_limit().name().empty()) {
        // If the descriptor has a name, append `;name="<DESCRIPTOR_NAME>"`
        absl::StrAppend(&quota_policy,
                        fmt::format(";{:s}=\"{:s}\"",
                                    Http::CustomHeaders::get().XRateLimitQuotaPolicyKeys.Name,
                                    status.current_limit().name()));
      }
    }
  }

  if (min_remaining_limit_status) {
    std::string rate_limit_limit = absl::StrCat(
        min_remaining_limit_status.value().current_limit().requests_per_unit(), quota_policy);
    result->addReferenceKey(Http::CustomHeaders::get().XRateLimitLimit, rate_limit_limit);
    result->addReferenceKey(Http::CustomHeaders::get().XRateLimitRemaining,
                            min_remaining_limit_status.value().limit_remaining());
    result->addReferenceKey(Http::CustomHeaders::get().XRateLimitReset,
                            min_remaining_limit_status.value().seconds_until_reset().seconds());
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
