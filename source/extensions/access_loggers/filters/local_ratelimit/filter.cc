#include "source/extensions/access_loggers/filters/local_ratelimit/filter.h"

#include "envoy/access_log/access_log.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace LocalRateLimit {
LocalRateLimitFilter::LocalRateLimitFilter(
    Server::Configuration::FactoryContext& context,
    const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter&
        config) {
  rate_limiter_ = Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      getRateLimiter(
          context, config.resource_name(), config.config_source(),
          [this](std::shared_ptr<
                 Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl>
                     limiter) -> void {
            ASSERT(limiter != nullptr, "limiter shouldn't be null if the `limiter` is set from "
                                       "callback.");
            rate_limiter_->setLimiter(limiter);
          });
}

bool LocalRateLimitFilter::evaluate(const Formatter::HttpFormatterContext&,
                                    const StreamInfo::StreamInfo&) const {
  ENVOY_BUG(rate_limiter_->getLimiter() != nullptr,
            "rate_limiter_.limiter_ should be already set in the callback put in `getRateLimiter` "
            "call in made the constructor.");
  return rate_limiter_->getLimiter()->requestAllowed({}).allowed;
}

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
