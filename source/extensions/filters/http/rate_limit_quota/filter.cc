#include "source/extensions/filters/http/rate_limit_quota/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  if (rate_limit_client_->startStream() == true) {
    rate_limit_client_->rateLimit();
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
