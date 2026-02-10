#include "source/extensions/filters/http/ratelimit/ratelimit_headers.h"

#include <cstddef>
#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/common/ratelimit_headers.h"

#include "absl/strings/substitute.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

bool enableXRateLimitHeaders(const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                             size_t index, bool default_enabled) {
  // Per our protocol, the returned statuses should map 1:1 to the descriptors we sent.
  // And the index should always be valid. But in case of any unexpected mismatch, we
  // fall back to the default value.
  if (index >= descriptors.size()) {
    return default_enabled;
  }
  if (descriptors[index].x_ratelimit_option_ == RateLimit::RateLimitProto::UNSPECIFIED) {
    return default_enabled;
  }
  return descriptors[index].x_ratelimit_option_ == RateLimit::RateLimitProto::DRAFT_VERSION_03;
}

void appendQuotaPolicy(std::string& out, size_t unit, size_t w, absl::string_view name) {
  // Constructing the quota-policy per RFC
  // https://tools.ietf.org/id/draft-polli-ratelimit-headers-02.html#name-ratelimit-limit
  // Example of the result: `, 10;w=1;name="per-ip", 1000;w=3600`
  // For each descriptor status append `<LIMIT>;w=<WINDOW_IN_SECONDS>`
  absl::SubstituteAndAppend(&out, ", $0;$1=$2", unit, Common::RateLimit::QuotaPolicyWindow, w);
  if (name.empty()) {
    return;
  }
  absl::SubstituteAndAppend(&out, ";$0=\"$1\"", Common::RateLimit::QuotaPolicyName, name);
}

void XRateLimitHeaderUtils::populateHeaders(
    const std::vector<Envoy::RateLimit::Descriptor>& descriptors, bool enabled,
    const Filters::Common::RateLimit::DescriptorStatusList& statuses,
    Http::ResponseHeaderMap& headers) {
  using LimitStatus = envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus;
  absl::optional<size_t> min_remaining_limit_status_index;
  OptRef<const LimitStatus> min_remaining_limit_status;

  // Get the descriptor status with the minimum remaining limit.
  for (size_t i = 0; i < statuses.size(); ++i) {
    const auto& status = statuses[i];
    if (!status.has_current_limit()) {
      continue;
    }

    if (!min_remaining_limit_status_index.has_value() ||
        status.limit_remaining() < min_remaining_limit_status->limit_remaining()) {
      min_remaining_limit_status_index.emplace(i);
      min_remaining_limit_status = OptRef<const LimitStatus>(status);
    }
  }

  if (!min_remaining_limit_status_index.has_value()) {
    return;
  }

  // If ratelimit headers are not enabled for the minimum remaining limit descriptor,
  // skip populating the headers.
  if (!enableXRateLimitHeaders(descriptors, *min_remaining_limit_status_index, enabled)) {
    return;
  }

  // Now we could populate the quota policy portion of the X-RateLimit-Limit header.
  std::string quota_policy;
  quota_policy.reserve(64);
  for (size_t i = 0; i < statuses.size(); ++i) {
    const auto& status = statuses[i];
    if (!status.has_current_limit() || !enableXRateLimitHeaders(descriptors, i, enabled)) {
      continue;
    }
    const uint32_t window = convertRateLimitUnit(status.current_limit().unit());
    if (window == 0) {
      continue;
    }
    appendQuotaPolicy(quota_policy, status.current_limit().requests_per_unit(), window,
                      status.current_limit().name());
  }

  const std::string rate_limit_limit =
      absl::StrCat(min_remaining_limit_status->current_limit().requests_per_unit(), quota_policy);
  headers.addReferenceKey(HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit,
                          rate_limit_limit);
  headers.addReferenceKey(
      HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining,
      min_remaining_limit_status->limit_remaining());
  headers.addReferenceKey(HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset,
                          min_remaining_limit_status->duration_until_reset().seconds());
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
  case envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::WEEK:
    return 7 * 24 * 60 * 60;
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
