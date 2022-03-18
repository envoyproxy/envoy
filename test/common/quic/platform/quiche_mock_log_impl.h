#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "source/common/common/assert.h"

#include "gmock/gmock.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

// A QuicheEnvoyMockLog object captures QUICHE_LOG() messages emitted between StartCapturingLogs()
// and destruction(or StopCapturingLogs()).
class QuicheEnvoyMockLog : public quiche::QuicheLogSink {
public:
  QuicheEnvoyMockLog() = default;

  ~QuicheEnvoyMockLog() override {
    if (is_capturing_) {
      StopCapturingLogs();
    }
  }

  MOCK_METHOD(void, Log, (QuicheLogLevel level, const std::string& message));

  // NOLINTNEXTLINE(readability-identifier-naming)
  void StartCapturingLogs() {
    ASSERT(!is_capturing_);
    is_capturing_ = true;
    original_sink_ = SetLogSink(this);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void StopCapturingLogs() {
    ASSERT(is_capturing_);
    is_capturing_ = false;
    SetLogSink(original_sink_);
  }

private:
  quiche::QuicheLogSink* original_sink_;
  bool is_capturing_{false};
};

// ScopedDisableExitOnDFatal is used to disable exiting the program when we encounter a
// QUICHE_LOG(DFATAL) within the current block. After we leave the current block, the previous
// behavior is restored.
class ScopedDisableExitOnDFatal {
public:
  ScopedDisableExitOnDFatal() : previous_value_(isDFatalExitDisabled()) {
    quiche::setDFatalExitDisabled(true);
  }

  ScopedDisableExitOnDFatal(const ScopedDisableExitOnDFatal&) = delete;
  ScopedDisableExitOnDFatal& operator=(const ScopedDisableExitOnDFatal&) = delete;

  ~ScopedDisableExitOnDFatal() { setDFatalExitDisabled(previous_value_); }

private:
  const bool previous_value_;
};

} // namespace quiche

using QuicheMockLogImpl = quiche::QuicheEnvoyMockLog;

#define CREATE_QUICHE_MOCK_LOG_IMPL(log) QuicheMockLog log

#define EXPECT_QUICHE_LOG_CALL_IMPL(log) EXPECT_CALL(log, Log(testing::_, testing::_))

#define EXPECT_QUICHE_LOG_CALL_CONTAINS_IMPL(log, level, content)                                  \
  EXPECT_CALL(log, Log(static_cast<quiche::QuicheLogLevel>(quiche::LogLevel##level),               \
                       testing::HasSubstr(content)))

// Not part of the api exposed by quiche_mock_log.h. This is used by
// quiche_expect_bug_impl.h.
#define EXPECT_QUICHE_LOG_IMPL(statement, level, matcher)                                          \
  do {                                                                                             \
    quiche::QuicheEnvoyMockLog mock_log;                                                           \
    EXPECT_CALL(mock_log,                                                                          \
                Log(static_cast<quiche::QuicheLogLevel>(quiche::LogLevel##level), matcher))        \
        .Times(testing::AtLeast(1));                                                               \
    mock_log.StartCapturingLogs();                                                                 \
    { statement; }                                                                                 \
    mock_log.StopCapturingLogs();                                                                  \
    if (!testing::Mock::VerifyAndClear(&mock_log)) {                                               \
      GTEST_NONFATAL_FAILURE_("");                                                                 \
    }                                                                                              \
  } while (false)

#define EXPECT_QUICHE_DFATAL_IMPL(statement, matcher)                                              \
  EXPECT_QUICHE_LOG_IMPL(                                                                          \
      {                                                                                            \
        quiche::ScopedDisableExitOnDFatal disable_exit_on_dfatal;                                  \
        statement;                                                                                 \
      },                                                                                           \
      DFATAL, matcher)
