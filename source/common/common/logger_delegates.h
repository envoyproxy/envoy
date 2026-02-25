#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/matching/common_actions/v3/actions.pb.h"
#include "envoy/extensions/matching/common_inputs/log_entry/v3/inputs.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/matcher/matcher.h"

#include "absl/strings/string_view.h"
#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Logger {

/**
 * SinkDelegate that writes log messages to a file.
 */
class FileSinkDelegate : public SinkDelegate {
public:
  static absl::StatusOr<std::unique_ptr<FileSinkDelegate>>
  create(const std::string& log_path, AccessLog::AccessLogManager& log_manager,
         DelegatingLogSinkSharedPtr log_sink);
  ~FileSinkDelegate() override;

  // SinkDelegate
  void log(absl::string_view msg, const spdlog::details::log_msg& log_msg) override;
  void flush() override;

protected:
  FileSinkDelegate(AccessLog::AccessLogFileSharedPtr&& log_file,
                   DelegatingLogSinkSharedPtr log_sink);

private:
  AccessLog::AccessLogFileSharedPtr log_file_;
};

#define EVENT_PIPE_STATS(COUNTER) COUNTER(write_failed)

struct EventPipeStats {
  EVENT_PIPE_STATS(GENERATE_COUNTER_STRUCT)
};

/** Match data for application logging. */
struct LogEntryData {
  static absl::string_view name() { return "log_entry_data"; }
  absl::string_view event_name_;
};
struct LogEntryActionContext {};

class EventNameInputFactory : public Matcher::DataInputFactory<LogEntryData> {
public:
  std::string name() const override { return "event_name_input"; }
  Matcher::DataInputFactoryCb<LogEntryData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::log_entry::v3::EventNameInput>();
  }
};

class DropActionFactory : public Matcher::ActionFactory<LogEntryActionContext> {
public:
  std::string name() const override { return "otlp_metric_drop_action_factory"; }
  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message&, LogEntryActionContext&,
                                             ProtobufMessage::ValidationVisitor&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::common_actions::v3::DropAction>();
  }
};

/**
 * Event sink delegate using a pipe. Note that writes to pipes are atomic as
 * long as each write size is below PIPE_BUF (which is 64kB on Linux). As long as
 * the serialized events are under this size, there is no need for
 * synchronization, unlike the regular logs.
 */
class EventPipeDelegate : public SinkDelegate {
public:
  static absl::StatusOr<std::unique_ptr<EventPipeDelegate>>
  create(Server::Configuration::ServerFactoryContext& server_factory_context,
         const std::string& log_path, const xds::type::matcher::v3::Matcher& filter_matcher,
         DelegatingLogSinkSharedPtr log_sink);
  ~EventPipeDelegate() override;

  // SinkDelegate
  void log(absl::string_view, const spdlog::details::log_msg& log_msg) override;
  void logWithStableName(absl::string_view stable_name, absl::string_view level,
                         absl::string_view component, absl::string_view msg) override;
  void flush() override;

protected:
  EventPipeDelegate(Filesystem::FilePtr file, Matcher::MatchTreePtr<LogEntryData> filter,
                    Stats::Scope& scope, DelegatingLogSinkSharedPtr log_sink);

private:
  Filesystem::FilePtr file_;
  Matcher::MatchTreePtr<LogEntryData> filter_;
  EventPipeStats stats_;
};

} // namespace Logger

} // namespace Envoy
