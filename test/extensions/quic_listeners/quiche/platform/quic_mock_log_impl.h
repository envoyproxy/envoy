#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

// A QuicEnvoyMockLog object captures QUIC_LOG() messages emitted between StartCapturingLogs() and
// destruction(or StopCapturingLogs()).
class QuicEnvoyMockLog : public QuicLogSink {
public:
  QuicEnvoyMockLog() = default;

  ~QuicEnvoyMockLog() override {
    if (is_capturing_) {
      StopCapturingLogs();
    }
  }

  MOCK_METHOD(void, Log, (QuicLogLevel level, const std::string& message));

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
  bool is_capturing_{false};
};

// ScopedDisableExitOnDFatal is used to disable exiting the program when we encounter a
// QUIC_LOG(DFATAL) within the current block. After we leave the current block, the previous
// behavior is restored.
class ScopedDisableExitOnDFatal {
public:
  ScopedDisableExitOnDFatal() : previous_value_(IsDFatalExitDisabled()) {
    SetDFatalExitDisabled(true);
  }

  ScopedDisableExitOnDFatal(const ScopedDisableExitOnDFatal&) = delete;
  ScopedDisableExitOnDFatal& operator=(const ScopedDisableExitOnDFatal&) = delete;

  ~ScopedDisableExitOnDFatal() { SetDFatalExitDisabled(previous_value_); }

private:
  const bool previous_value_;
};

} // namespace quic

using QuicMockLogImpl = quic::QuicEnvoyMockLog;

#define CREATE_QUIC_MOCK_LOG_IMPL(log) QuicMockLog log

#define EXPECT_QUIC_LOG_CALL_IMPL(log) EXPECT_CALL(log, Log(testing::_, testing::_))

#define EXPECT_QUIC_LOG_CALL_CONTAINS_IMPL(log, level, content)                                    \
  EXPECT_CALL(log, Log(quic::level, testing::HasSubstr(content)))

// Not part of the api exposed by quic_mock_log.h. This is used by
// quic_expect_bug_impl.h.
#define EXPECT_QUIC_LOG_IMPL(statement, level, matcher)                                            \
  do {                                                                                             \
    quic::QuicEnvoyMockLog mock_log;                                                               \
    EXPECT_CALL(mock_log, Log(quic::level, matcher)).Times(testing::AtLeast(1));                   \
    mock_log.StartCapturingLogs();                                                                 \
    { statement; }                                                                                 \
    mock_log.StopCapturingLogs();                                                                  \
    if (!testing::Mock::VerifyAndClear(&mock_log)) {                                               \
      GTEST_NONFATAL_FAILURE_("");                                                                 \
    }                                                                                              \
  } while (false)

#define EXPECT_QUIC_DFATAL_IMPL(statement, matcher)                                                \
  EXPECT_QUIC_LOG_IMPL(                                                                            \
      {                                                                                            \
        quic::ScopedDisableExitOnDFatal disable_exit_on_dfatal;                                    \
        statement;                                                                                 \
      },                                                                                           \
      DFATAL, matcher)
