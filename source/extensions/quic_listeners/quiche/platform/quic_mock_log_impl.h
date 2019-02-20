#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "common/common/assert.h"

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

#include "gmock/gmock.h"

namespace quic {

class QuicEnvoyMockLog : public QuicLogSink {
public:
  QuicEnvoyMockLog() : is_capturing_(false) {}

  virtual ~QuicEnvoyMockLog() {
    if (is_capturing_) {
      StopCapturingLogs();
    }
  }

  MOCK_METHOD2(Log, void(QuicLogLevel level, const std::string& message));

  void StartCapturingLogs() {
    ASSERT(!is_capturing_);
    is_capturing_ = true;
    original_sink_ = SetLogSink(this);
  }

  void StopCapturingLogs() {
    ASSERT(is_capturing_);
    is_capturing_ = false;
    SetLogSink(original_sink_);
  }

private:
  QuicLogSink* original_sink_;
  bool is_capturing_;
};

/*
enum LogCapturingState_ { kDoNotCaptureLogsYet };

class ScopedMockLog : public LogSink {
 public:
  // When a ScopedMockLog object is created via the default
  // constructor, it starts to capture logs. This can be a problem if
  // some other threads already exist and are logging, as the user
  // hasn't had a chance to set up expectation on this object yet
  // (calling a mock method before setting the expectation is
  // UNDEFINED behavior).
  ABSL_DEPRECATED("Use the other constructor in new code.")
  ScopedMockLog() : is_capturing_logs_(false) { StartCapturingLogs(); }

  // A user can use the syntax
  //   ScopedMockLog log(kDoNotCaptureLogsYet);
  // to invoke this constructor. A ScopedMockLog object created this way
  // does not start capturing logs until StartCapturingLogs() is called.
  explicit ScopedMockLog(LogCapturingState_)
      : is_capturing_logs_(false) {}

  // When the object is destructed, it stops intercepting logs.
  virtual ~ScopedMockLog() { if (is_capturing_logs_) StopCapturingLogs(); }

  // Starts log capturing if the object isn't already doing so.
  // Otherwise crashes. Usually this method is called in the same
  // thread that created this object. It is the user's responsibility
  // to not call this method if another thread may be calling it or
  // StopCapturingLogs() at the same time.
  void StartCapturingLogs() {
    // We don't use CHECK(), which can generate a new LOG message, and
    // thus can confuse ScopedMockLog objects or other registered
    // LogSinks.
    RAW_CHECK(!is_capturing_logs_,
              "StartCapturingLogs() can be called only when the ScopedMockLog "
              "object is not capturing logs.");

    is_capturing_logs_ = true;
    AddLogSink(this);
  }

  // Stops log capturing if the object is capturing logs. Otherwise
  // crashes. Usually this method is called in the same thread that
  // created this object. It is the user's responsibility to not call
  // this method if another thread may be calling it or
  // StartCapturingLogs() at the same time.
  void StopCapturingLogs() {
    // We don't use CHECK(), which can generate a new LOG message, and
    // thus can confuse ScopedMockLog objects or other registered
    // LogSinks.
    RAW_CHECK(is_capturing_logs_,
              "StopCapturingLogs() can be called only when the ScopedMockLog "
              "object is capturing logs.");

    is_capturing_logs_ = false;
    RemoveLogSink(this);
  }

  // Implements the mock method:
  //
  //   void Log(LogSeverity severity, const string& file_path,
  //            const string& message);
  //
  // The second argument to Send() is the full path of the source file
  // in which the LOG() was issued.
  //
  // Note, that in a multi-threaded environment, all LOG() messages from a
  // single thread will be handled in sequence, but that cannot be guaranteed
  // for messages from different threads. In fact, if the same or multiple
  // expectations are matched on two threads concurrently, their actions will
  // be executed concurrently as well and may interleave.
  MOCK_METHOD3(Log, void(base_logging::LogSeverity severity,
                         const string& file_path, const string& message));

 private:
  // Implements the Send() virtual function in class LogSink.
  // Whenever a LOG() statement is executed, this function will be
  // invoked with information presented in the LOG().
  //
  // TODO(wan): call Log() inside Send() and get rid of WaitTillSent().
  virtual void Send(const absl::LogEntry& entry) {
    // We are only interested in the log severity, full file name, and
    // log message.
    MessageInfo* message_info = message_info_.pointer();
    message_info->severity =
        static_cast<base_logging::LogSeverity>(entry.log_severity());
    message_info->file_path = entry.source_filename();
    message_info->message = string(entry.text_message());
  }

  // Implements the WaitTillSent() virtual function in class LogSink.
  // It will be executed after Send() and after the global logging lock is
  // released.
  //
  // LOG(), Send(), WaitTillSent() and Log() will occur in the same thread for
  // a given log message.
  virtual void WaitTillSent() {
    // First, we save a copy of the message being processed before
    // calling Log().
    MessageInfo message_info = message_info_.get();
    Log(message_info.severity, message_info.file_path, message_info.message);
  }

  // All relevant information about a logged message that needs to be passed
  // from Send() to WaitTillSent().
  struct MessageInfo {
    base_logging::LogSeverity severity;
    string file_path;
    string message;
  };

  bool is_capturing_logs_;
  ThreadLocal<MessageInfo> message_info_;
};
*/
} // namespace quic

using QuicMockLogImpl = quic::QuicEnvoyMockLog;

#define CREATE_QUIC_MOCK_LOG_IMPL(log) QuicMockLog log

#define EXPECT_QUIC_LOG_CALL_IMPL(log) EXPECT_CALL(log, Log(testing::_, testing::_))

#define EXPECT_QUIC_LOG_CALL_CONTAINS_IMPL(log, level, content)                                    \
  EXPECT_CALL(log, Log(quic::level, testing::HasSubstr(content)))
