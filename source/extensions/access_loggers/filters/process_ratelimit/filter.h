#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/access_loggers/filters/process_ratelimit/v3/process_ratelimit.pb.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/access_loggers/filters/process_ratelimit/provider_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {

#define ALL_PROCESS_RATELIMIT_FILTER_STATS(COUNTER)                                                \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)

/**
 * Struct definition for all process ratelimit filter stats. @see stats_macros.h
 */
struct ProcessRateLimitFilterStats {
  ALL_PROCESS_RATELIMIT_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};
class ProcessRateLimitFilter : public AccessLog::Filter {
public:
  ProcessRateLimitFilter(Server::Configuration::ServerFactoryContext& context,
                         const envoy::extensions::access_loggers::filters::process_ratelimit::v3::
                             ProcessRateLimitFilter& config);

  bool evaluate(const Formatter::Context& log_context,
                const StreamInfo::StreamInfo& stream_info) const override;

  ~ProcessRateLimitFilter() override;

private:
  const intptr_t setter_key_;
  std::shared_ptr<std::atomic<bool>> cancel_cb_;
  Server::Configuration::ServerFactoryContext& context_;
  ProcessRateLimitFilterStats stats_;
  mutable Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      RateLimiterWrapperPtr rate_limiter_;
};

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
