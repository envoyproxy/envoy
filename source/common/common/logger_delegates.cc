#include "source/common/common/logger_delegates.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>

#include "envoy/registry/registry.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {
absl::StatusOr<std::unique_ptr<FileSinkDelegate>>
FileSinkDelegate::create(const std::string& log_path, AccessLog::AccessLogManager& log_manager,
                         DelegatingLogSinkSharedPtr log_sink) {
  auto file_or_error = log_manager.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, log_path});
  RETURN_IF_NOT_OK_REF(file_or_error.status());
  return std::unique_ptr<FileSinkDelegate>(
      new FileSinkDelegate(std::move(*file_or_error), log_sink));
}

FileSinkDelegate::FileSinkDelegate(AccessLog::AccessLogFileSharedPtr&& log_file,
                                   DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink), log_file_(std::move(log_file)) {
  setDelegate();
}

FileSinkDelegate::~FileSinkDelegate() { restoreDelegate(); }

void FileSinkDelegate::log(absl::string_view msg, const spdlog::details::log_msg&) {
  // Log files have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  log_file_->write(msg);
}

void FileSinkDelegate::flush() {
  // Log files have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  log_file_->flush();
}

EventPipeDelegate::EventPipeDelegate(Filesystem::FilePtr file,
                                     Matcher::MatchTreePtr<LogEntryData> filter,
                                     Stats::Scope& scope, DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(std::move(log_sink)), file_(std::move(file)), filter_(std::move(filter)),
      stats_{EVENT_PIPE_STATS(POOL_COUNTER_PREFIX(scope, "event_log."))} {
  setDelegate();
}

EventPipeDelegate::~EventPipeDelegate() { restoreDelegate(); }

void EventPipeDelegate::log(absl::string_view msg, const spdlog::details::log_msg& log_msg) {
  previousDelegate()->log(msg, log_msg);
}

void EventPipeDelegate::logWithStableName(absl::string_view stable_name, absl::string_view level,
                                          absl::string_view component, absl::string_view msg) {
  previousDelegate()->logWithStableName(stable_name, level, component, msg);
  if (const auto result =
          Matcher::evaluateMatch(*filter_, LogEntryData{.event_name_ = stable_name});
      result.isMatch()) {
    if (result.action()->typeUrl() ==
        "type.googleapis.com/envoy.extensions.matching.common_actions.v3.DropAction") {
      return;
    }
    ENVOY_LOG_EVERY_POW_2_MISC(warn, "Unknown log entry action: {}", result.action()->typeUrl());
    return;
  }
  const std::string data =
      absl::StrCat("[", level, "] ", component, " ", stable_name, " ", msg, "\n");
  const Api::IoCallSizeResult result = file_->write(data);
  if (!result.ok()) {
    // Since O_NONBLOCK is enabled, EAGAIN is returned when there is no capacity in the file buffer.
    stats_.write_failed_.inc();
    if (result.return_value_ > 0 && result.return_value_ != static_cast<ssize_t>(data.size())) {
      ENVOY_LOG_EVERY_POW_2_MISC(warn, "Partial write to the event log, please increase PIPE_BUF "
                                       "size to be larger than the log entry size.");
    }
  }
}

void EventPipeDelegate::flush() { previousDelegate()->flush(); }

namespace {
constexpr Filesystem::FlagSet DefaultFlags =
    1 << Filesystem::File::Operation::Write | 1 << Filesystem::File::Operation::Create |
    1 << Filesystem::File::Operation::Append | 1 << Filesystem::File::Operation::NonBlock;

class LogEntryActionValidationVisitor : public Matcher::MatchTreeValidationVisitor<LogEntryData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<LogEntryData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};
} // namespace

absl::StatusOr<std::unique_ptr<EventPipeDelegate>>
EventPipeDelegate::create(Server::Configuration::ServerFactoryContext& server_factory_context,
                          const std::string& log_path,
                          const xds::type::matcher::v3::Matcher& filter_matcher,
                          DelegatingLogSinkSharedPtr log_sink) {
  Filesystem::FilePtr file = server_factory_context.api().fileSystem().createFile(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, log_path});
  const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
  if (!open_result.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("unable to open file '{}': {}", log_path, open_result.err_->getErrorDetails()));
  }
  LogEntryActionContext action_context;
  LogEntryActionValidationVisitor validation_visitor;
  Matcher::MatchTreeFactory<LogEntryData, LogEntryActionContext> factory{
      action_context, server_factory_context, validation_visitor};
  Matcher::MatchTreePtr<LogEntryData> filter = factory.create(filter_matcher)();
  return std::unique_ptr<EventPipeDelegate>(
      new EventPipeDelegate(std::move(file), std::move(filter),
                            server_factory_context.serverScope(), std::move(log_sink)));
}

Matcher::DataInputFactoryCb<LogEntryData>
EventNameInputFactory::createDataInputFactoryCb(const Protobuf::Message&,
                                                ProtobufMessage::ValidationVisitor&) {
  class EventNameInput : public Matcher::DataInput<LogEntryData> {
  public:
    Matcher::DataInputGetResult get(const LogEntryData& data) const override {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(data.event_name_)};
    }
  };
  return [] { return std::make_unique<EventNameInput>(); };
}

Matcher::ActionConstSharedPtr DropActionFactory::createAction(const Protobuf::Message&,
                                                              LogEntryActionContext&,
                                                              ProtobufMessage::ValidationVisitor&) {
  class DropAction
      : public Matcher::ActionBase<envoy::extensions::matching::common_actions::v3::DropAction> {};
  return std::make_shared<DropAction>();
}

REGISTER_FACTORY(EventNameInputFactory, Matcher::DataInputFactory<LogEntryData>);
REGISTER_FACTORY(DropActionFactory, Matcher::ActionFactory<LogEntryActionContext>);

} // namespace Logger
} // namespace Envoy
