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
        config)
    : context_(context), config_(config) {}

bool LocalRateLimitFilter::evaluate(const Formatter::HttpFormatterContext&,
                                    const StreamInfo::StreamInfo&) const {
  ENVOY_BUG(rate_limiter_ != nullptr,
            "rate_limiter_ should be set by init_target_'s init callback.");
  ENVOY_BUG(rate_limiter_->getLimiter() != nullptr,
            "LocalRateLimitFilter::evaluate: rate_limiter_ should be "
            "initialized by init() but it is null now.");
  return rate_limiter_->getLimiter()->requestAllowed({}).allowed;
}

void LocalRateLimitFilter::init() {
  init_target_ = std::make_unique<Envoy::Init::TargetImpl>("local_ratelimit_filter",
                                                           [this] { initializeRateLimiter(); });
  context_.initManager().add(*init_target_);
}

void LocalRateLimitFilter::initializeRateLimiter() {
  rate_limiter_ = Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      getRateLimiter(
          context_, config_.resource_name(), config_.config_source(),
          [this](std::shared_ptr<
                 Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterImpl>
                     limiter) -> void {
            if (limiter == nullptr) {
              return;
            }
            rate_limiter_->setLimiter(limiter);
            init_target_->ready();
          });

  if (rate_limiter_->getLimiter() != nullptr) {
    init_target_->ready();
  }
}

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
