#include "source/server/utils.h"

#include "envoy/common/exception.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/assert.h"
#include "source/server/generic_factory_context.h"

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

namespace {

class LogFormatterProvider : public Formatter::FormatterProvider {
public:
  using FieldExtractor = std::function<absl::string_view(const LogExtension&)>;
  explicit LogFormatterProvider(FieldExtractor f) : field_extractor_(f) {}

  // FormatterProvider
  absl::optional<std::string> formatWithContext(const Formatter::Context& ctx,
                                                const StreamInfo::StreamInfo&) const override {
    return std::string(field_extractor_(*ctx.typedExtension<LogExtension>()));
  }
  Protobuf::Value formatValueWithContext(const Formatter::Context& ctx,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    return ValueUtil::optionalStringValue(formatWithContext(ctx, stream_info));
  }

private:
  FieldExtractor field_extractor_;
};

using LogFormatterProviderPtr = std::unique_ptr<LogFormatterProvider>;
using FormatterProviderCreateFunc = std::function<LogFormatterProviderPtr()>;
using FormatterLookupTable = absl::flat_hash_map<absl::string_view, FormatterProviderCreateFunc>;

const FormatterLookupTable& formatterLookupTable() {
  CONSTRUCT_ON_FIRST_USE(FormatterLookupTable,
                         {
                             {"MESSAGE",
                              []() {
                                return std::make_unique<LogFormatterProvider>(
                                    [](const LogExtension& ext) { return ext.msg_; });
                              }},
                             {"COMPONENT",
                              []() {
                                return std::make_unique<LogFormatterProvider>(
                                    [](const LogExtension& ext) { return ext.component_; });
                              }},
                             {"LEVEL",
                              []() {
                                return std::make_unique<LogFormatterProvider>(
                                    [](const LogExtension& ext) { return ext.level_; });
                              }},
                             {"EVENT",
                              []() {
                                return std::make_unique<LogFormatterProvider>(
                                    [](const LogExtension& ext) { return ext.event_; });
                              }},

                         });
}

class ApplicationLogCommandParser : public Formatter::CommandParser {
public:
  Formatter::FormatterProviderPtr parse(absl::string_view command, absl::string_view,
                                        absl::optional<size_t>) const override {
    auto it = formatterLookupTable().find(command);
    if (it != formatterLookupTable().end()) {
      return it->second();
    }
    return nullptr;
  }
};

using Parsers = std::vector<Formatter::CommandParserPtr>;
Parsers logCommandParsers() {
  Parsers parsers;
  parsers.push_back(std::make_unique<ApplicationLogCommandParser>());
  return parsers;
}

} // namespace

absl::StatusOr<std::unique_ptr<ApplicationLogSink>> maybeAddApplicationLogSink(
    const envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig& application_log_config,
    Server::Configuration::ServerFactoryContext& context) {
  if (const auto& proto_sinks = application_log_config.log_sinks(); proto_sinks.size() > 0) {
    std::vector<AccessLog::InstanceSharedPtr> logs;
    logs.reserve(proto_sinks.size());
    TRY_ASSERT_MAIN_THREAD {
      GenericFactoryContextImpl generic_context(context, context.messageValidationVisitor());
      for (const auto& config : proto_sinks) {
        logs.push_back(
            AccessLog::AccessLogFactory::fromProto(config, generic_context, logCommandParsers()));
      }
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      return absl::InvalidArgumentError(
          fmt::format("Failed to initialize application logs: {}", e.what()));
    });
    return std::make_unique<ApplicationLogSink>(std::move(logs), context.timeSource(),
                                                Logger::Registry::getSink());
  }
  return nullptr;
}

ApplicationLogSink::ApplicationLogSink(std::vector<AccessLog::InstanceSharedPtr>&& logs,
                                       TimeSource& time_source,
                                       Envoy::Logger::DelegatingLogSinkSharedPtr log_sink)
    : Envoy::Logger::SinkDelegate(log_sink), logs_(std::move(logs)),
      info_(time_source, nullptr, StreamInfo::FilterState::LifeSpan::Connection) {
  setDelegate();
}

ApplicationLogSink::~ApplicationLogSink() { restoreDelegate(); }

void ApplicationLogSink::log(absl::string_view message, const spdlog::details::log_msg& log_msg) {
  if (auto* sink = previousDelegate(); sink != nullptr) {
    sink->log(message, log_msg);
  }
  Formatter::Context formatter_ctx;
  LogExtension ctx(formatter_ctx);
  ctx.msg_ = absl::string_view(log_msg.payload.data(), log_msg.payload.size());
  ctx.component_ = absl::string_view(log_msg.logger_name.data(), log_msg.logger_name.size());
  const auto level = spdlog::level::to_string_view(log_msg.level);
  ctx.level_ = absl::string_view(level.data(), level.size());
  for (auto& logger : logs_) {
    logger->log(formatter_ctx, info_);
  }
}

void ApplicationLogSink::logWithStableName(absl::string_view stable_name, absl::string_view level,
                                           absl::string_view component, absl::string_view msg) {
  if (auto* sink = previousDelegate(); sink != nullptr) {
    sink->logWithStableName(stable_name, level, component, msg);
  }
  Formatter::Context formatter_ctx;
  LogExtension ctx(formatter_ctx);
  ctx.msg_ = msg;
  ctx.component_ = component;
  ctx.level_ = level;
  ctx.event_ = stable_name;
  for (auto& logger : logs_) {
    logger->log(formatter_ctx, info_);
  }
}

void ApplicationLogSink::flush() {
  if (auto* sink = previousDelegate(); sink != nullptr) {
    sink->flush();
  }
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
