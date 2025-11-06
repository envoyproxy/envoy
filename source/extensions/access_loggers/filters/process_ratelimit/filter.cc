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
    : setter_key_(reinterpret_cast<intptr_t>(this)),
      cancel_cb_(std::make_shared<std::atomic<bool>>(false)), context_(context),
      stats_({ALL_PROCESS_RATELIMIT_FILTER_STATS(
          POOL_COUNTER_PREFIX(context.scope(), "access_log.process_ratelimit."))}) {
  auto setter =
      [this, cancel_cb = cancel_cb_](
          Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterSharedPtr limiter)
      -> void {
    if (!cancel_cb->load()) {
      ENVOY_BUG(limiter != nullptr, "limiter shouldn't be null if the `limiter` is set from "
                                    "callback.");
      rate_limiter_->setLimiter(limiter);
    }
  };

  if (!config.has_dynamic_config()) {
    ExceptionUtil::throwEnvoyException("`dynamic_config` is required.");
  }
  rate_limiter_ = Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      getRateLimiter(context, config.dynamic_config().resource_name(),
                     config.dynamic_config().config_source(), setter_key_, std::move(setter));
}

ProcessRateLimitFilter::~ProcessRateLimitFilter() {
  // The destructor can be called in any thread.
  // The `cancel_cb_` is set to true to prevent the `limiter` from being set in
  // the `setter` from the main thread.
  cancel_cb_->store(true);
  context_.mainThreadDispatcher().post(
      [limiter = std::move(rate_limiter_), setter_key = setter_key_] {
        // remove the setter for this filter.
        limiter->getSubscription()->removeSetter(setter_key);
      });
}

bool ProcessRateLimitFilter::evaluate(const Formatter::Context&,
                                      const StreamInfo::StreamInfo&) const {
  ENVOY_BUG(rate_limiter_->getLimiter() != nullptr,
            "rate_limiter_.limiter_ should be already set in init callback.");
  Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterSharedPtr limiter =
      rate_limiter_->getLimiter();
  auto result = limiter->requestAllowed({});
  if (!result.allowed) {
    stats_.denied_.inc();
  } else {
    stats_.allowed_.inc();
  }
  return result.allowed;
}

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
