#include "source/server/utils.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Server {
namespace Utility {

envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed) {
  switch (state) {
  case Init::Manager::State::Uninitialized:
    return envoy::admin::v3::ServerInfo::PRE_INITIALIZING;
  case Init::Manager::State::Initializing:
    return envoy::admin::v3::ServerInfo::INITIALIZING;
  case Init::Manager::State::Initialized:
    return health_check_failed ? envoy::admin::v3::ServerInfo::DRAINING
                               : envoy::admin::v3::ServerInfo::LIVE;
  }
  IS_ENVOY_BUG("unexpected server state enum");
  return envoy::admin::v3::ServerInfo::PRE_INITIALIZING;
}

absl::Status assertExclusiveLogFormatMethod(
    const Options& options,
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config) {
  if (options.logFormatSet() && application_log_config.has_log_format()) {
    return absl::InvalidArgumentError(
        "Only one of ApplicationLogConfig.log_format or CLI option --log-format can be specified.");
  }
  return absl::OkStatus();
}

absl::Status maybeSetApplicationLogFormat(
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config) {
  if (!application_log_config.has_log_format()) {
    return absl::OkStatus();
  }

  if (application_log_config.log_format().has_text_format()) {
    Logger::Registry::setLogFormat(application_log_config.log_format().text_format());
  } else if (application_log_config.log_format().has_json_format()) {
    const auto status =
        Logger::Registry::setJsonLogFormat(application_log_config.log_format().json_format());

    if (!status.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("setJsonLogFormat error: {}", status.ToString()));
    }
  }
  return absl::OkStatus();
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
