#include "source/extensions/access_loggers/filters/process_ratelimit/filter.h"

#include "envoy/access_log/access_log.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {

ProcessRateLimitFilter::ProcessRateLimitFilter(
    Server::Configuration::ServerFactoryContext& context,
    const envoy::extensions::access_loggers::filters::process_ratelimit::v3::ProcessRateLimitFilter&
        config)
    : cancel_cb_(std::make_shared<bool>(false)) {
  auto setter =
      [this, cancel_cb = std::shared_ptr<bool>(cancel_cb_)](
          Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterSharedPtr limiter)
      -> void {
    if (*cancel_cb) {
      return;
    }
    ENVOY_BUG(limiter != nullptr, "limiter shouldn't be null if the `limiter` is set from "
                                  "callback.");

    rate_limiter_->setLimiter(limiter);
  };

  if (!config.has_dynamic_config()) {
    ExceptionUtil::throwEnvoyException("`dynamic_config` is required.");
  }
  rate_limiter_ = Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      getRateLimiter(context, config.dynamic_config().resource_name(),
                     config.dynamic_config().config_source(), std::move(setter));
}

ProcessRateLimitFilter::~ProcessRateLimitFilter() { *cancel_cb_ = true; }

bool ProcessRateLimitFilter::evaluate(const Formatter::HttpFormatterContext&,
                                      const StreamInfo::StreamInfo&) const {
  ENVOY_BUG(rate_limiter_->getLimiter() != nullptr,
            "rate_limiter_.limiter_ should be already set in init callback.");
  auto limiter = rate_limiter_->getLimiter();
  return limiter->requestAllowed({}).allowed;
}

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
