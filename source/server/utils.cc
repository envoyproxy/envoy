#include "source/server/utils.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/access_log/access_log_impl.h"

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

absl::StatusOr<std::unique_ptr<ApplicationLogSink>> maybeAddApplicationLogSink(
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config,
    Server::Configuration::ServerFactoryContext& context) {
  if (const auto& proto_sinks = application_log_config.log_sinks(); proto_sinks.size() > 0) {
    std::vector<AccessLog::InstanceSharedPtr> logs;
    logs.reserve(proto_sinks.size());
    TRY_ASSERT_MAIN_THREAD {
    for (const auto& config : proto_sinks) {
        logs.push_back(AccessLog::AccessLogFactory::fromProto(config, context, {}));
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      return absl::InvalidArgumentError(
          fmt::format("Failed to initialize application logs: {}", e.what()));
    });
    return std::make_unique<ApplicationLogSink>(std::move(logs), Logger::Registry::getSink());
  }
  return nullptr;
}

ApplicationLogSink::ApplicationLogSink(
    std::vector<AccessLog::InstanceSharedPtr>&& logs,
    Envoy::Logger::DelegatingLogSinkSharedPtr log_sink)
    : Envoy::Logger::SinkDelegate(log_sink), logs_(std::move(logs)) {
  setDelegate();
}

ApplicationLogSink::~ApplicationLogSink() { restoreDelegate(); }

void ApplicationLogSink::log(absl::string_view message,
         const spdlog::details::log_msg& log_msg) {
  if (auto* sink = previousDelegate(); sink != nullptr) {
    sink->log(message, log_msg);
  }
}

void ApplicationLogSink::logWithStableName(absl::string_view stable_name, absl::string_view level,
                               absl::string_view component, absl::string_view msg) {
  if (auto* sink = previousDelegate(); sink != nullptr) {
    sink->logWithStableName(stable_name, level, component, msg);
  }
}

void ApplicationLogSink::flush() {}

} // namespace Utility
} // namespace Server
} // namespace Envoy
