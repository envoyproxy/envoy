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

// A QuicEnvoyMockLog object captures QUIC_LOG() messages emitted between StartCapturingLogs() and
// destruction(or StopCapturingLogs()).
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

} // namespace quic

using QuicMockLogImpl = quic::QuicEnvoyMockLog;

#define CREATE_QUIC_MOCK_LOG_IMPL(log) QuicMockLog log

#define EXPECT_QUIC_LOG_CALL_IMPL(log) EXPECT_CALL(log, Log(testing::_, testing::_))

#define EXPECT_QUIC_LOG_CALL_CONTAINS_IMPL(log, level, content)                                    \
  EXPECT_CALL(log, Log(quic::level, testing::HasSubstr(content)))
