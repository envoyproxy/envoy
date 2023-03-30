#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/common/callback.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Upstream {

class HealthCheckEventLoggerImpl : public HealthCheckEventLogger {
public:
  HealthCheckEventLoggerImpl(AccessLog::AccessLogManager& log_manager, TimeSource& time_source,
                             const std::string& file_name)
      : time_source_(time_source), file_(log_manager.createAccessLog(Filesystem::FilePathAndType{
                                       Filesystem::DestinationType::File, file_name})) {}

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

private:
  void createHealthCheckEvent(
      envoy::data::core::v3::HealthCheckerType health_checker_type, const HostDescription& host,
      std::function<void(envoy::data::core::v3::HealthCheckEvent&)> callback) const;
  TimeSource& time_source_;
  AccessLog::AccessLogFileSharedPtr file_;
};

} // namespace Upstream
} // namespace Envoy
