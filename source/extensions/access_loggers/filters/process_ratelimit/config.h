#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {

class ProcessRateLimitFilterFactory : public AccessLog::ExtensionFilterFactory {
public:
  AccessLog::FilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
                                    Server::Configuration::GenericFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.access_loggers.extension_filters.process_ratelimit";
  }
};

} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
