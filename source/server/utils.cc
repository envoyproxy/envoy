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

void assertExclusiveLogFormatMethod(const Options& options,
                                    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  if (options.logFormatSet() && bootstrap.has_application_log_format()) {
    throw EnvoyException(
        "Only one of application_log_format or CLI option --log-format can be specified.");
  }
}

void maybeSetApplicationLogFormat(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  if (bootstrap.has_application_log_format() &&
      bootstrap.application_log_format().has_json_format()) {
    const auto status =
        Logger::Registry::setJsonLogFormat(bootstrap.application_log_format().json_format());

    if (!status.ok()) {
      throw EnvoyException(fmt::format("setJsonLogFormat error: {}", status.ToString()));
    }
  }
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
