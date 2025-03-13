#include "source/common/common/logger.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "source/common/common/json_escape_string.h"
#include "source/common/common/lock_guard.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

StandardLogger::StandardLogger(const std::string& name)
    : Logger(std::make_shared<spdlog::logger>(name, Registry::getSink())) {}

SinkDelegate::SinkDelegate(DelegatingLogSinkSharedPtr log_sink) : log_sink_(log_sink) {}
void SinkDelegate::logWithStableName(absl::string_view, absl::string_view, absl::string_view,
                                     absl::string_view) {}

SinkDelegate::~SinkDelegate() {
  // The previous delegate should have never been set or should have been reset by now via
  // restoreDelegate()/restoreTlsDelegate();
  assert(previous_delegate_ == nullptr);
  assert(previous_tls_delegate_ == nullptr);
}

void SinkDelegate::setTlsDelegate() {
  assert(previous_tls_delegate_ == nullptr);
  previous_tls_delegate_ = log_sink_->tlsDelegate();
  log_sink_->setTlsDelegate(this);
}

void SinkDelegate::setDelegate() {
  // There should be no previous delegate before this call.
  assert(previous_delegate_ == nullptr);
  previous_delegate_ = log_sink_->delegate();
  log_sink_->setDelegate(this);
}

void SinkDelegate::restoreTlsDelegate() {
  // Ensures stacked allocation of delegates.
  assert(log_sink_->tlsDelegate() == this);
  log_sink_->setTlsDelegate(previous_tls_delegate_);
  previous_tls_delegate_ = nullptr;
}

void SinkDelegate::restoreDelegate() {
  // Ensures stacked allocation of delegates.
  assert(log_sink_->delegate() == this);
  log_sink_->setDelegate(previous_delegate_);
  previous_delegate_ = nullptr;
}

StderrSinkDelegate::StderrSinkDelegate(DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink) {
  setDelegate();
}

StderrSinkDelegate::~StderrSinkDelegate() { restoreDelegate(); }

void StderrSinkDelegate::log(absl::string_view msg, const spdlog::details::log_msg&) {
  Thread::OptionalLockGuard guard(lock_);
  std::cerr << msg;
}

void StderrSinkDelegate::flush() {
  Thread::OptionalLockGuard guard(lock_);
  std::cerr << std::flush;
}

void DelegatingLogSink::set_formatter(std::unique_ptr<spdlog::formatter> formatter) {
  absl::MutexLock lock(&format_mutex_);
  formatter_ = std::move(formatter);
}

void DelegatingLogSink::log(const spdlog::details::log_msg& msg) {
  absl::ReleasableMutexLock lock(&format_mutex_);
  absl::string_view msg_view = absl::string_view(msg.payload.data(), msg.payload.size());

  // This memory buffer must exist in the scope of the entire function,
  // otherwise the string_view will refer to memory that is already free.
  spdlog::memory_buf_t formatted;
  if (formatter_) {
    formatter_->format(msg, formatted);
    msg_view = absl::string_view(formatted.data(), formatted.size());
  }
  lock.Release();

  auto log_to_sink = [this, msg_view, msg](SinkDelegate& sink) {
    if (should_escape_) {
      sink.log(escapeLogLine(msg_view), msg);
    } else {
      sink.log(msg_view, msg);
    }
  };
  auto* tls_sink = tlsDelegate();
  if (tls_sink != nullptr) {
    log_to_sink(*tls_sink);
    return;
  }

  // Hold the sink mutex while performing the actual logging. This prevents the sink from being
  // swapped during an individual log event.
  // TODO(mattklein123): In production this lock will never be contended. In practice, thread
  // protection is really only needed in tests. It would be nice to figure out a test-only
  // mechanism for this that does not require extra locking that we don't explicitly need in the
  // prod code.
  absl::ReaderMutexLock sink_lock(&sink_mutex_);
  log_to_sink(*sink_);
}

std::string DelegatingLogSink::escapeLogLine(absl::string_view msg_view) {
  absl::string_view eol = spdlog::details::os::default_eol;
  if (!absl::EndsWith(msg_view, eol)) {
    // Log does not end with newline, escape entire log.
    return absl::CEscape(msg_view);
  }

  // Log line ends with newline. Escape everything except the end-of-line character to preserve line
  // format.
  absl::string_view msg_leading =
      absl::string_view(msg_view.data(), msg_view.size() - eol.length());
  return absl::StrCat(absl::CEscape(msg_leading), eol);
}

DelegatingLogSinkSharedPtr DelegatingLogSink::init() {
  DelegatingLogSinkSharedPtr delegating_sink(new DelegatingLogSink);
  delegating_sink->stderr_sink_ = std::make_unique<StderrSinkDelegate>(delegating_sink);
  return delegating_sink;
}

void DelegatingLogSink::flush() {
  auto* tls_sink = tlsDelegate();
  if (tls_sink != nullptr) {
    tls_sink->flush();
    return;
  }
  absl::ReaderMutexLock lock(&sink_mutex_);
  sink_->flush();
}

SinkDelegate** DelegatingLogSink::tlsSink() {
  static thread_local SinkDelegate* tls_sink = nullptr;

  return &tls_sink;
}

void DelegatingLogSink::setTlsDelegate(SinkDelegate* sink) { *tlsSink() = sink; }

SinkDelegate* DelegatingLogSink::tlsDelegate() { return *tlsSink(); }

void DelegatingLogSink::logWithStableName(absl::string_view stable_name, absl::string_view level,
                                          absl::string_view component, absl::string_view message) {
  auto tls_sink = tlsDelegate();
  if (tls_sink != nullptr) {
    tls_sink->logWithStableName(stable_name, level, component, message);
    return;
  }
  absl::ReaderMutexLock sink_lock(&sink_mutex_);
  sink_->logWithStableName(stable_name, level, component, message);
}

static std::atomic<Context*> current_context = nullptr;
static_assert(std::atomic<Context*>::is_always_lock_free);

Context::Context(spdlog::level::level_enum log_level, const std::string& log_format,
                 Thread::BasicLockable& lock, bool should_escape, bool enable_fine_grain_logging)
    : log_level_(log_level), log_format_(log_format), lock_(lock), should_escape_(should_escape),
      enable_fine_grain_logging_(enable_fine_grain_logging), save_context_(current_context) {
  current_context = this;
  activate();
}

Context::~Context() {
  current_context = save_context_;
  if (current_context != nullptr) {
    current_context.load()->activate();
  } else {
    Registry::getSink()->clearLock();
  }
}

void Context::activate() {
  Registry::getSink()->setLock(lock_);
  Registry::getSink()->setShouldEscape(should_escape_);
  Registry::setLogLevel(log_level_);
  Registry::setLogFormat(log_format_);

  // sets level and format for Fine-grain Logger
  fine_grain_default_level_ = log_level_;
  fine_grain_log_format_ = log_format_;
  if (enable_fine_grain_logging_) {
    if (log_format_ == Logger::Logger::DEFAULT_LOG_FORMAT) {
      fine_grain_log_format_ = kDefaultFineGrainLogFormat;
    }
    getFineGrainLogContext().setDefaultFineGrainLogLevelFormat(fine_grain_default_level_,
                                                               fine_grain_log_format_);
  }
}

bool Context::useFineGrainLogger() {
  if (current_context) {
    return current_context.load()->enable_fine_grain_logging_;
  }
  return false;
}

void Context::changeAllLogLevels(spdlog::level::level_enum level) {
  if (!useFineGrainLogger()) {
    ENVOY_LOG_MISC(info, "change all log levels: level='{}'",
                   spdlog::level::level_string_views[level]);
    Registry::setLogLevel(level);
  } else {
    // Level setting with Fine-Grain Logger.
    FINE_GRAIN_LOG(
        info,
        "change all log levels and default verbosity level for fine grain loggers: level='{}'",
        spdlog::level::level_string_views[level]);
    getFineGrainLogContext().updateVerbosityDefaultLevel(level);
  }
}

void Context::enableFineGrainLogger() {
  if (current_context) {
    current_context.load()->enable_fine_grain_logging_ = true;
    current_context.load()->fine_grain_default_level_ = current_context.load()->log_level_;
    current_context.load()->fine_grain_log_format_ = current_context.load()->log_format_;
    if (current_context.load()->log_format_ == Logger::Logger::DEFAULT_LOG_FORMAT) {
      current_context.load()->fine_grain_log_format_ = kDefaultFineGrainLogFormat;
    }
    getFineGrainLogContext().setDefaultFineGrainLogLevelFormat(
        current_context.load()->fine_grain_default_level_,
        current_context.load()->fine_grain_log_format_);
  }
}

void Context::disableFineGrainLogger() {
  if (current_context) {
    current_context.load()->enable_fine_grain_logging_ = false;
  }
}

std::string Context::getFineGrainLogFormat() {
  if (!current_context) { // Context is not instantiated in benchmark test
    return kDefaultFineGrainLogFormat;
  }
  return current_context.load()->fine_grain_log_format_;
}

spdlog::level::level_enum Context::getFineGrainDefaultLevel() {
  if (!current_context) {
    return spdlog::level::info;
  }
  return current_context.load()->fine_grain_default_level_;
}

std::vector<Logger>& Registry::allLoggers() {
  static std::vector<Logger>* all_loggers =
      new std::vector<Logger>({ALL_LOGGER_IDS(GENERATE_LOGGER)});
  return *all_loggers;
}

spdlog::logger& Registry::getLog(Id id) { return allLoggers()[static_cast<int>(id)].getLogger(); }

void Registry::setLogLevel(spdlog::level::level_enum log_level) {
  for (Logger& logger : allLoggers()) {
    logger.getLogger().set_level(static_cast<spdlog::level::level_enum>(log_level));
  }
}

void Registry::setLogFormat(const std::string& log_format) {
  for (Logger& logger : allLoggers()) {
    Utility::setLogFormatForLogger(logger.getLogger(), log_format);
  }

  json_log_format_set_ = false;
}

absl::Status Registry::setJsonLogFormat(const Protobuf::Message& log_format_struct) {
#ifndef ENVOY_ENABLE_YAML
  UNREFERENCED_PARAMETER(log_format_struct);
  return absl::UnimplementedError("JSON/YAML support compiled out");
#else
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  json_options.always_print_fields_with_no_presence = true;

  std::string format_as_json;
  const auto status =
      Protobuf::util::MessageToJsonString(log_format_struct, &format_as_json, json_options);

  if (!status.ok()) {
    return absl::InvalidArgumentError("Provided struct cannot be serialized as JSON string");
  }

  if (format_as_json.find("%v") != std::string::npos) {
    return absl::InvalidArgumentError("Usage of %v is unavailable for JSON log formats");
  }

  if (format_as_json.find("%_") != std::string::npos) {
    return absl::InvalidArgumentError("Usage of %_ is unavailable for JSON log formats");
  }

  // Since the format is now a JSON struct, it is guaranteed that it ends with '}'.
  // Here we replace '}' with '%*}' to enable the option to add more JSON properties,
  // in case ENVOY_TAGGED_LOG is used. If there are no tags, '%*' will be replaced by
  // an empty string, falling back to the original log format. If there are log tags,
  // '%*' will be replaced by the serialized tags as JSON properties.
  format_as_json.replace(format_as_json.rfind('}'), 1, "%*}");

  // To avoid performance impact for '%j' flag in case JSON logs are not enabled,
  // all occurrences of '%j' will be replaced with '%+' which removes the tags from
  // the message string before writing it to the output as escaped JSON string.
  size_t flag_index = format_as_json.find("%j");
  while (flag_index != std::string::npos) {
    format_as_json.replace(flag_index, 2, "%+");
    flag_index = format_as_json.find("%j");
  }

  setLogFormat(format_as_json);
  json_log_format_set_ = true;
  return absl::OkStatus();
#endif
}

bool Registry::json_log_format_set_ = false;

Logger* Registry::logger(const std::string& log_name) {
  Logger* logger_to_return = nullptr;
  for (Logger& logger : loggers()) {
    if (logger.name() == log_name) {
      logger_to_return = &logger;
      break;
    }
  }
  return logger_to_return;
}
namespace Utility {

void setLogFormatForLogger(spdlog::logger& logger, const std::string& log_format) {
  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter
      ->add_flag<CustomFlagFormatter::EscapeMessageNewLine>(
          CustomFlagFormatter::EscapeMessageNewLine::Placeholder)
      .set_pattern(log_format);

  // Escape log payload as JSON string when it sees "%j".
  formatter
      ->add_flag<CustomFlagFormatter::EscapeMessageJsonString>(
          CustomFlagFormatter::EscapeMessageJsonString::Placeholder)
      .set_pattern(log_format);

  formatter
      ->add_flag<CustomFlagFormatter::ExtractedTags>(
          CustomFlagFormatter::ExtractedTags::Placeholder)
      .set_pattern(log_format);

  formatter
      ->add_flag<CustomFlagFormatter::ExtractedMessage>(
          CustomFlagFormatter::ExtractedMessage::Placeholder)
      .set_pattern(log_format);

  logger.set_formatter(std::move(formatter));
}

std::string serializeLogTags(const std::map<std::string, std::string>& tags) {
  if (tags.empty()) {
    return "";
  }

  std::stringstream tags_stream;
  tags_stream << TagsPrefix;
  if (Registry::jsonLogFormatSet()) {
    for (const auto& tag : tags) {
      tags_stream << "\"";

      auto required_space = JsonEscaper::extraSpace(tag.first);
      if (required_space == 0) {
        tags_stream << tag.first;
      } else {
        tags_stream << JsonEscaper::escapeString(tag.first, required_space);
      }
      tags_stream << "\":\"";

      required_space = JsonEscaper::extraSpace(tag.second);
      if (required_space == 0) {
        tags_stream << tag.second;
      } else {
        tags_stream << JsonEscaper::escapeString(tag.second, required_space);
      }
      tags_stream << "\",";
    }
  } else {
    for (const auto& tag : tags) {
      tags_stream << "\"" << tag.first << "\":\"" << tag.second << "\",";
    }
  }

  std::string serialized = tags_stream.str();
  serialized.pop_back();
  serialized += TagsSuffix;

  return serialized;
}

void escapeMessageJsonString(absl::string_view payload, spdlog::memory_buf_t& dest) {
  const uint64_t required_space = JsonEscaper::extraSpace(payload);
  if (required_space == 0) {
    dest.append(payload.data(), payload.data() + payload.size());
    return;
  }

  const std::string escaped = JsonEscaper::escapeString(payload, required_space);
  dest.append(escaped.data(), escaped.data() + escaped.size());
}

} // namespace Utility

namespace CustomFlagFormatter {

void EscapeMessageNewLine::format(const spdlog::details::log_msg& msg, const std::tm&,
                                  spdlog::memory_buf_t& dest) {
  const std::string escaped = absl::StrReplaceAll(
      absl::string_view(msg.payload.data(), msg.payload.size()), replacements());
  dest.append(escaped.data(), escaped.data() + escaped.size());
}

void EscapeMessageJsonString::format(const spdlog::details::log_msg& msg, const std::tm&,
                                     spdlog::memory_buf_t& dest) {

  absl::string_view payload = absl::string_view(msg.payload.data(), msg.payload.size());
  Envoy::Logger::Utility::escapeMessageJsonString(payload, dest);
}

void ExtractedTags::format(const spdlog::details::log_msg& msg, const std::tm&,
                           spdlog::memory_buf_t& dest) {
  absl::string_view payload = absl::string_view(msg.payload.data(), msg.payload.size());
  if (payload.rfind(Utility::TagsPrefix, 0) == std::string::npos) {
    return;
  }

  auto tags_end_pos = payload.find(Utility::TagsSuffixForSearch) + 1;
  dest.append(&JsonPropertyDeimilter, &JsonPropertyDeimilter + 1);
  dest.append(payload.data() + Utility::TagsPrefix.size(), payload.data() + tags_end_pos);
}

void ExtractedMessage::format(const spdlog::details::log_msg& msg, const std::tm&,
                              spdlog::memory_buf_t& dest) {
  absl::string_view payload = absl::string_view(msg.payload.data(), msg.payload.size());
  if (payload.rfind(Utility::TagsPrefix, 0) == std::string::npos) {
    Envoy::Logger::Utility::escapeMessageJsonString(payload, dest);
    return;
  }

  auto tags_end_pos =
      payload.find(Utility::TagsSuffixForSearch) + Utility::TagsSuffixForSearch.size();
  auto original_message =
      absl::string_view(payload.data() + tags_end_pos, payload.size() - tags_end_pos);
  Envoy::Logger::Utility::escapeMessageJsonString(original_message, dest);
}

} // namespace CustomFlagFormatter
} // namespace Logger
} // namespace Envoy
