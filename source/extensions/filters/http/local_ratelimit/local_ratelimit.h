#pragma once

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/common/ratelimit_headers.h"
#include "source/extensions/filters/http/local_ratelimit/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

/**
 * HTTP local rate limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(FilterConfigSharedPtr config) : config_(config), used_config_(config_.get()) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

private:
  friend class FilterTest;

  void populateDescriptors(std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           Http::RequestHeaderMap& headers);
  void populateDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                           std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           Http::RequestHeaderMap& headers);
  VhRateLimitOptions getVirtualHostRateLimitOption(const Router::RouteConstSharedPtr& route);
  Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl& getPerConnectionRateLimiter();
  Extensions::Filters::Common::LocalRateLimit::LocalRateLimiter::Result
  requestAllowed(absl::Span<const Envoy::RateLimit::Descriptor> request_descriptors);
  bool enableXRateLimitHeaders() const {
    if (x_ratelimit_option_ ==
        Envoy::RateLimit::XRateLimitOption::RateLimit_XRateLimitOption_UNSPECIFIED) {
      return used_config_->enableXRateLimitHeaders();
    }
    return x_ratelimit_option_ ==
           Envoy::RateLimit::XRateLimitOption::RateLimit_XRateLimitOption_DRAFT_VERSION_03;
  }

  FilterConfigSharedPtr config_;
  // Actual config used for the current request. Is config_ by default, but can be overridden by
  // per-route config.
  const FilterConfig* used_config_{};
  std::shared_ptr<const Extensions::Filters::Common::LocalRateLimit::TokenBucketContext>
      token_bucket_context_;
  Envoy::RateLimit::XRateLimitOption x_ratelimit_option_{};

  VhRateLimitOptions vh_rate_limits_;
};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
