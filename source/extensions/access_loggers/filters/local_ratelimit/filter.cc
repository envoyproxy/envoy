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
    Server::Configuration::ServerFactoryContext& context,
    const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter&
        config)
    : cancel_cb_(std::make_shared<bool>(false)) {
  rate_limiter_wrapper_ = Envoy::Extensions::Filters::Common::LocalRateLimit::
      RateLimiterProviderSingleton::getRateLimiter(
          context, config.resource_name(), config.config_source(),
          [this, cancel_cb = std::shared_ptr<bool>(cancel_cb_)](
              std::shared_ptr<
                  Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl>
                  limiter) -> void {
            if (*cancel_cb) {
              return;
            }
            rate_limiter_wrapper_->setLimiter(limiter);
          });
}

LocalRateLimitFilter::~LocalRateLimitFilter() { *cancel_cb_ = true; }

bool LocalRateLimitFilter::evaluate(const Formatter::HttpFormatterContext&,
                                    const StreamInfo::StreamInfo&) const {
  ENVOY_BUG(rate_limiter_wrapper_->getLimiter() != nullptr,
            "rate_limiter_wrapper_.limiter_ should be already set in the callback put in "
            "`getRateLimiter` "
            "call in made the constructor.");
  return rate_limiter_wrapper_->getLimiter()->requestAllowed({}).allowed;
}

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
