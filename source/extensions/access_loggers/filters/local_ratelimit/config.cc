#include "source/extensions/access_loggers/filters/local_ratelimit/config.h"

#include "envoy/extensions/access_loggers/filters/local_ratelimit/v3/local_ratelimit.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/access_loggers/filters/local_ratelimit/filter.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace LocalRateLimit {

constexpr absl::string_view kRateLimitFilterDefaultKey = "access_log_rate_limit_default_key";

AccessLog::FilterPtr LocalRateLimitFilterFactory::createFilter(
    const envoy::config::accesslog::v3::ExtensionFilter& config,
    Server::Configuration::FactoryContext& context) {
  auto factory_config =
      Config::Utility::translateToFactoryConfig(config, context.messageValidationVisitor(), *this);
  const auto& local_ratelimit_config = dynamic_cast<
      const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter&>(
      *factory_config);

  const absl::string_view key = local_ratelimit_config.key().empty() ? kRateLimitFilterDefaultKey
                                                                     : local_ratelimit_config.key();
  auto rate_limiter =
      Envoy::Extensions::Filters::Common::LocalRateLimit::LocalRateLimiterMapSingleton::
          getRateLimiter(context.serverFactoryContext().singletonManager(), key,
                         std::chrono::milliseconds(Protobuf::util::TimeUtil::DurationToMilliseconds(
                             local_ratelimit_config.token_bucket().fill_interval())),
                         local_ratelimit_config.token_bucket().max_tokens(),
                         PROTOBUF_GET_WRAPPED_OR_DEFAULT(local_ratelimit_config.token_bucket(),
                                                         tokens_per_fill, 1),
                         context.serverFactoryContext().mainThreadDispatcher(), {},
                         /*always_consume_default_token_bucket=*/false,
                         /*shared_provider=*/nullptr, /*lru_size=*/0);
  return std::make_unique<LocalRateLimitFilter>(std::move(rate_limiter));
}

ProtobufTypes::MessagePtr LocalRateLimitFilterFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter>();
}

REGISTER_FACTORY(LocalRateLimitFilterFactory, AccessLog::ExtensionFilterFactory);

} // namespace LocalRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
