#include "source/extensions/access_loggers/filters/process_ratelimit/config.h"

#include "envoy/extensions/access_loggers/filters/process_ratelimit/v3/process_ratelimit.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/access_loggers/filters/process_ratelimit/filter.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {

AccessLog::FilterPtr ProcessRateLimitFilterFactory::createFilter(
    const envoy::config::accesslog::v3::ExtensionFilter& config,
    Server::Configuration::GenericFactoryContext& context) {
  auto factory_config =
      Config::Utility::translateToFactoryConfig(config, context.messageValidationVisitor(), *this);
  const auto& process_ratelimit_config =
      dynamic_cast<const envoy::extensions::access_loggers::filters::process_ratelimit::v3::
                       ProcessRateLimitFilter&>(*factory_config);
  auto filter = std::make_unique<ProcessRateLimitFilter>(context.serverFactoryContext(),
                                                         process_ratelimit_config);
  return filter;
}

ProtobufTypes::MessagePtr ProcessRateLimitFilterFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::filters::process_ratelimit::v3::ProcessRateLimitFilter>();
}

REGISTER_FACTORY(ProcessRateLimitFilterFactory, AccessLog::ExtensionFilterFactory);

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
