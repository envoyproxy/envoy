#pragma once

#include "envoy/access_log/access_log.h"

#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace LocalRateLimit {

class LocalRateLimitFilter : public AccessLog::Filter {
public:
  LocalRateLimitFilter(Envoy::Extensions::Filters::Common::LocalRateLimit::
                           LocalRateLimiterMapSingleton::RateLimiter&& rate_limiter)
      : rate_limiter_(std::move(rate_limiter)) {}

  bool evaluate(const Formatter::HttpFormatterContext&,
                const StreamInfo::StreamInfo&) const override {
    return rate_limiter_.limiter_->requestAllowed({}).allowed;
  }

private:
  mutable Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterMapSingleton::
      RateLimiter rate_limiter_;
};

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
