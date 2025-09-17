#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/access_loggers/filters/local_ratelimit/v3/local_ratelimit.pb.h"

#include "source/common/init/target_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace LocalRateLimit {

class LocalRateLimitFilter : public AccessLog::Filter {
public:
  LocalRateLimitFilter(
      Server::Configuration::FactoryContext& context,
      const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter&
          config);

  bool evaluate(const Formatter::HttpFormatterContext&,
                const StreamInfo::StreamInfo&) const override;

  void init();

private:
  void initializeRateLimiter();

  Server::Configuration::FactoryContext& context_;
  const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter
      config_;
  mutable Envoy::Extensions::Filters::Common::LocalRateLimit::
      RateLimiterProviderSingleton::RateLimiterWrapperPtr rate_limiter_;
  mutable std::unique_ptr<Init::TargetImpl> init_target_;
};

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
