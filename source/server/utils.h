#pragma once

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/options.h"
#include "envoy/server/factory_context.h"
#include "envoy/access_log/access_log.h"

#include "source/common/common/logger_delegates.h"

namespace Envoy {
namespace Server {
namespace Utility {

/**
 * This class configures the application log sinks.
 */
class ApplicationLogSink : public Logger::SinkDelegate {
public:
  explicit ApplicationLogSink(
      std::vector<AccessLog::InstanceSharedPtr>&& logs,
      Envoy::Logger::DelegatingLogSinkSharedPtr log_sink);

  ~ApplicationLogSink() override;

  // SinkDelegate
  void log(absl::string_view message,
           const spdlog::details::log_msg& log_msg) override;
  void logWithStableName(absl::string_view stable_name, absl::string_view level,
                                 absl::string_view component, absl::string_view msg) override;
  void flush() override;

private:
  const std::vector<AccessLog::InstanceSharedPtr> logs_;
};

/*
    Fetches the current state of the server (e.g., initializing, live, etc.)
    given the manager's state and the status of the health check.
*/
envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

absl::Status assertExclusiveLogFormatMethod(
    const Options& options,
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config);

absl::Status maybeSetApplicationLogFormat(
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config);

absl::StatusOr<std::unique_ptr<ApplicationLogSink>> maybeAddApplicationLogSink(
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config,
    Server::Configuration::ServerFactoryContext& context);

} // namespace Utility
} // namespace Server
} // namespace Envoy
