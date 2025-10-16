#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/access_loggers/filters/process_ratelimit/v3/process_ratelimit.pb.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/filters/common/local_ratelimit/provider_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {

class ProcessRateLimitFilter : public AccessLog::Filter {
public:
  ProcessRateLimitFilter(Server::Configuration::ServerFactoryContext& context,
                         const envoy::extensions::access_loggers::filters::process_ratelimit::v3::
                             ProcessRateLimitFilter& config);

  bool evaluate(const Formatter::HttpFormatterContext&,
                const StreamInfo::StreamInfo&) const override;

  ~ProcessRateLimitFilter() override;

private:
  const intptr_t setter_key_;
  std::shared_ptr<std::atomic<bool>> cancel_cb_;
  Server::Configuration::ServerFactoryContext& context_;
  mutable Envoy::Extensions::Filters::Common::LocalRateLimit::RateLimiterProviderSingleton::
      RateLimiterWrapperPtr rate_limiter_;
};

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
