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
#include "envoy/upstream/health_check_event_sink.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

class HealthCheckEventLoggerImpl : public HealthCheckEventLogger {
public:
  HealthCheckEventLoggerImpl(const envoy::config::core::v3::HealthCheck& health_check_config,
                             Server::Configuration::HealthCheckerFactoryContext& context)
      : time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {
    // TODO(botengyao): Remove the file_ creation here into the file based health check
    // event sink. In this way you can remove the file_ based code from the createHealthCheckEvent
    if (!health_check_config.event_log_path().empty() /* deprecated */) {
      auto file_or_error = context.serverFactoryContext().accessLogManager().createAccessLog(
          Filesystem::FilePathAndType{Filesystem::DestinationType::File,
                                      health_check_config.event_log_path()});
      THROW_IF_NOT_OK_REF(file_or_error.status());
      file_ = file_or_error.value();
    }
    for (const auto& config : health_check_config.event_logger()) {
      auto& factory = Config::Utility::getAndCheckFactory<HealthCheckEventSinkFactory>(config);
      event_sinks_.push_back(factory.createHealthCheckEventSink(config.typed_config(), context));
    }
  }

  void logEjectUnhealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                         const HostDescriptionConstSharedPtr& host,
                         envoy::data::core::v3::HealthCheckFailureType failure_type) override;
  void logAddHealthy(envoy::data::core::v3::HealthCheckerType health_checker_type,
                     const HostDescriptionConstSharedPtr& host, bool first_check) override;
  void logSuccessfulHealthCheck(envoy::data::core::v3::HealthCheckerType health_checker_type,
                                const HostDescriptionConstSharedPtr& host) override;
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
  std::vector<HealthCheckEventSinkPtr> event_sinks_;
};

} // namespace Upstream
} // namespace Envoy
