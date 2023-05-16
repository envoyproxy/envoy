#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/common/callback.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

class HealthCheckEventLoggerImpl : public HealthCheckEventLogger {
public:
  HealthCheckEventLoggerImpl(AccessLog::AccessLogManager& log_manager, TimeSource& time_source,
                             const envoy::config::core::v3::HealthCheck& health_check_config,
                             Server::Configuration::CommonFactoryContext& context)
      : time_source_(time_source), context_(context) {
    if (!health_check_config.event_log_path().empty()) {
      file_ = log_manager.createAccessLog(Filesystem::FilePathAndType{
          Filesystem::DestinationType::File, health_check_config.event_log_path()});
    }

    for (const auto& config : health_check_config.event_logger()) {
      if (!config.has_typed_config())
        continue;
      const auto& validator_config =
          Envoy::MessageUtil::anyConvertAndValidate<envoy::config::accesslog::v3::AccessLog>(
              config.typed_config(), context.messageValidationVisitor());
      access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(validator_config, context));
    }
  }

  void logEjectUnhealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                         const HostDescriptionConstSharedPtr& host,
                         envoy::data::core::v3::HealthCheckFailureType failure_type) override;
  void logAddHealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                     const HostDescriptionConstSharedPtr& host, bool first_check) override;
  void logUnhealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                    const HostDescriptionConstSharedPtr& host,
                    envoy::data::core::v3::HealthCheckFailureType failure_type,
                    bool first_check) override;
  void logDegraded(envoy::data::core::v3::HealthCheckerType health_checker_type,
                   const HostDescriptionConstSharedPtr& host) override;
  void logNoLongerDegraded(envoy::data::core::v3::HealthCheckerType health_checker_type,
                           const HostDescriptionConstSharedPtr& host) override;

  std::vector<AccessLog::InstanceSharedPtr> accessLogs() const { return access_logs_; };

private:
  void createHealthCheckEvent(
      envoy::data::core::v3::HealthCheckerType health_checker_type, const HostDescription& host,
      std::function<void(envoy::data::core::v3::HealthCheckEvent&)> callback) const;
  TimeSource& time_source_;
  Server::Configuration::CommonFactoryContext& context_;
  AccessLog::AccessLogFileSharedPtr file_;
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
};

} // namespace Upstream
} // namespace Envoy
