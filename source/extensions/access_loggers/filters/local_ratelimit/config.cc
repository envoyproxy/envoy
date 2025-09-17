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

AccessLog::FilterPtr LocalRateLimitFilterFactory::createFilter(
    const envoy::config::accesslog::v3::ExtensionFilter& config,
    Server::Configuration::FactoryContext& context) {
  auto factory_config =
      Config::Utility::translateToFactoryConfig(config, context.messageValidationVisitor(), *this);
  const auto& local_ratelimit_config = dynamic_cast<
      const envoy::extensions::access_loggers::filters::local_ratelimit::v3::LocalRateLimitFilter&>(
      *factory_config);
  auto filter = std::make_unique<LocalRateLimitFilter>(context, local_ratelimit_config);
  return filter;
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
