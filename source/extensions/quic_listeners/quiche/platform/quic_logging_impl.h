#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/stl_helpers.h"

#include "absl/base/optimization.h"
#include "absl/synchronization/mutex.h"

// This implementation is only used by Quiche code, use macros provided by
// assert.h and logger.h in Envoy code instead. See QUIC platform API
// dependency model described in
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/README.md
//
// The implementation is backed by Envoy::Logger.

// If |condition| is true, use |logstream| to stream the log message and send it to spdlog.
// If |condition| is false, |logstream| will not be instantiated.
// The switch(0) is used to suppress a compiler warning on ambiguous "else".
#define QUICHE_LOG_IMPL_INTERNAL(condition, logstream)                                             \
  switch (0)                                                                                       \
  default:                                                                                         \
    if (!(condition)) {                                                                            \
    } else                                                                                         \
      logstream

#define QUICHE_LOG_IF_IMPL(severity, condition)                                                    \
  QUICHE_LOG_IMPL_INTERNAL(                                                                        \
      QUICHE_IS_LOG_LEVEL_ENABLED(severity) && (condition),                                        \
      quic::QuicLogEmitter(quic::severity, __FILE__, __LINE__, __func__).stream())

#define QUICHE_LOG_IMPL(severity) QUICHE_LOG_IF_IMPL(severity, true)

#define QUICHE_VLOG_IF_IMPL(verbosity, condition)                                                  \
  QUICHE_LOG_IMPL_INTERNAL(                                                                        \
      quic::isVerboseLogEnabled(verbosity) && (condition),                                         \
      quic::QuicLogEmitter(quic::INFO, __FILE__, __LINE__, __func__).stream())

#define QUICHE_VLOG_IMPL(verbosity) QUICHE_VLOG_IF_IMPL(verbosity, true)

// TODO(wub): Implement QUICHE_LOG_FIRST_N_IMPL.
#define QUICHE_LOG_FIRST_N_IMPL(severity, n) QUICHE_LOG_IMPL(severity)

// TODO(wub): Implement QUICHE_LOG_EVERY_N_IMPL.
#define QUICHE_LOG_EVERY_N_IMPL(severity, n) QUICHE_LOG_IMPL(severity)

// TODO(wub): Implement QUICHE_LOG_EVERY_N_SEC_IMPL.
#define QUICHE_LOG_EVERY_N_SEC_IMPL(severity, seconds) QUICHE_LOG_IMPL(severity)

#define QUICHE_PLOG_IMPL(severity)                                                                 \
  QUICHE_LOG_IMPL_INTERNAL(                                                                        \
      QUICHE_IS_LOG_LEVEL_ENABLED(severity),                                                       \
      quic::QuicLogEmitter(quic::severity, __FILE__, __LINE__, __func__).SetPerror().stream())

#define QUICHE_LOG_INFO_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(INFO)
#define QUICHE_LOG_WARNING_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(WARNING)
#define QUICHE_LOG_ERROR_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(ERROR)

#define QUICHE_CHECK_INNER_IMPL(condition, details)                                                \
  QUICHE_LOG_IF_IMPL(FATAL, ABSL_PREDICT_FALSE(!(condition))) << details << ". "

#define QUICHE_CHECK_IMPL(condition) QUICHE_CHECK_INNER_IMPL(condition, "CHECK failed: " #condition)

#define QUICHE_CHECK_INNER_IMPL_OP(op, a, b)                                                       \
  QUICHE_CHECK_INNER_IMPL((a)op(b),                                                                \
                          "CHECK failed: " #a " (=" << (a) << ") " #op " " #b " (=" << (b) << ")")

#define QUICHE_CHECK_GT_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(>, a, b)
#define QUICHE_CHECK_GE_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(>=, a, b)
#define QUICHE_CHECK_LT_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(<, a, b)
#define QUICHE_CHECK_LE_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(<=, a, b)
#define QUICHE_CHECK_NE_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(!=, a, b)
#define QUICHE_CHECK_EQ_IMPL(a, b) QUICHE_CHECK_INNER_IMPL_OP(==, a, b)

#ifdef NDEBUG
// Release build
#define QUICHE_DCHECK_IMPL(condition) QUICHE_COMPILED_OUT_LOG(condition)
#define QUICHE_COMPILED_OUT_LOG(condition)                                                         \
  QUICHE_LOG_IMPL_INTERNAL(false && (condition), quic::NullLogStream().stream())
#define QUICHE_DVLOG_IMPL(verbosity) QUICHE_COMPILED_OUT_LOG(false)
#define QUICHE_DVLOG_IF_IMPL(verbosity, condition) QUICHE_COMPILED_OUT_LOG(condition)
#define QUICHE_DLOG_IMPL(severity) QUICHE_COMPILED_OUT_LOG(false)
#define QUICHE_DLOG_IF_IMPL(severity, condition) QUICHE_COMPILED_OUT_LOG(condition)
#define QUICHE_DLOG_INFO_IS_ON_IMPL() 0
#define QUICHE_DLOG_EVERY_N_IMPL(severity, n) QUICHE_COMPILED_OUT_LOG(false)
#define QUICHE_NOTREACHED_IMPL()
#define QUICHE_DCHECK_GT_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) > (b))
#define QUICHE_DCHECK_GE_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) >= (b))
#define QUICHE_DCHECK_LT_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) < (b))
#define QUICHE_DCHECK_LE_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) <= (b))
#define QUICHE_DCHECK_NE_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) != (b))
#define QUICHE_DCHECK_EQ_IMPL(a, b) QUICHE_COMPILED_OUT_LOG((a) == (b))
#else
// Debug build
#define QUICHE_DCHECK_IMPL(condition) QUICHE_CHECK_IMPL(condition)
#define QUICHE_DVLOG_IMPL(verbosity) QUICHE_VLOG_IMPL(verbosity)
#define QUICHE_DVLOG_IF_IMPL(verbosity, condition) QUICHE_VLOG_IF_IMPL(verbosity, condition)
#define QUICHE_DLOG_IMPL(severity) QUICHE_LOG_IMPL(severity)
#define QUICHE_DLOG_IF_IMPL(severity, condition) QUICHE_LOG_IF_IMPL(severity, condition)
#define QUICHE_DLOG_INFO_IS_ON_IMPL() QUICHE_LOG_INFO_IS_ON_IMPL()
#define QUICHE_DLOG_EVERY_N_IMPL(severity, n) QUICHE_LOG_EVERY_N_IMPL(severity, n)
#define QUICHE_NOTREACHED_IMPL() NOT_REACHED_GCOVR_EXCL_LINE
#define QUICHE_DCHECK_GT_IMPL(a, b) QUICHE_CHECK_GT_IMPL(a, b)
#define QUICHE_DCHECK_GE_IMPL(a, b) QUICHE_CHECK_GE_IMPL(a, b)
#define QUICHE_DCHECK_LT_IMPL(a, b) QUICHE_CHECK_LT_IMPL(a, b)
#define QUICHE_DCHECK_LE_IMPL(a, b) QUICHE_CHECK_LE_IMPL(a, b)
#define QUICHE_DCHECK_NE_IMPL(a, b) QUICHE_CHECK_NE_IMPL(a, b)
#define QUICHE_DCHECK_EQ_IMPL(a, b) QUICHE_CHECK_EQ_IMPL(a, b)
#endif

#define QUICHE_PREDICT_FALSE_IMPL(x) ABSL_PREDICT_FALSE(x)

namespace quic {

using QuicLogLevel = spdlog::level::level_enum;

static const QuicLogLevel TRACE = spdlog::level::trace;
static const QuicLogLevel QDEBUG = spdlog::level::debug;
static const QuicLogLevel INFO = spdlog::level::info;
static const QuicLogLevel WARNING = spdlog::level::warn;
static const QuicLogLevel ERROR = spdlog::level::err;
static const QuicLogLevel FATAL = spdlog::level::critical;

// DFATAL is FATAL in debug mode, ERROR in release mode.
#ifdef NDEBUG
static const QuicLogLevel DFATAL = ERROR;
#else
static const QuicLogLevel DFATAL = FATAL;
#endif

class QuicLogEmitter {
public:
  // |file_name| and |function_name| MUST be valid for the lifetime of the QuicLogEmitter. This is
  // guaranteed when passing __FILE__ and __func__.
  explicit QuicLogEmitter(QuicLogLevel level, const char* file_name, int line,
                          const char* function_name);

  ~QuicLogEmitter();

  QuicLogEmitter& SetPerror() {
    is_perror_ = true;
    return *this;
  }

  std::ostringstream& stream() { return stream_; }

private:
  const QuicLogLevel level_;
  const char* file_name_;
  int line_;
  const char* function_name_;
  const int saved_errno_;
  bool is_perror_ = false;
  std::ostringstream stream_;
};

class NullLogStream : public std::ostream {
public:
  NullLogStream() : std::ostream(nullptr) {}

  NullLogStream& stream() { return *this; }
};

template <typename T> inline NullLogStream& operator<<(NullLogStream& s, const T&) { return s; }

inline spdlog::logger& GetLogger() {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::quic);
}

// This allows us to use QUICHE_CHECK(condition) from constexpr functions.
#define QUICHE_IS_LOG_LEVEL_ENABLED(severity) quic::isLogLevelEnabled##severity()
#define QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(severity)                                                 \
  inline bool isLogLevelEnabled##severity() { return quic::severity >= GetLogger().level(); }
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(TRACE)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(QDEBUG)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(INFO)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(WARNING)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(ERROR)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(DFATAL)
inline bool constexpr isLogLevelEnabledFATAL() { return true; }

int getVerbosityLogThreshold();
void setVerbosityLogThreshold(int new_verbosity);

inline bool isVerboseLogEnabled(int verbosity) {
  return QUICHE_IS_LOG_LEVEL_ENABLED(INFO) && verbosity <= getVerbosityLogThreshold();
}

bool isDFatalExitDisabled();
void setDFatalExitDisabled(bool is_disabled);

// QuicLogSink is used to capture logs emitted from the QUICHE_LOG... macros.
class QuicLogSink {
public:
  virtual ~QuicLogSink() = default;

  // Called when |message| is emitted at |level|.
  virtual void Log(QuicLogLevel level, const std::string& message) = 0;
};

// Only one QuicLogSink can capture log at a time. SetLogSink causes future logs
// to be captured by the |new_sink|.
// Return the previous sink.
QuicLogSink* SetLogSink(QuicLogSink* new_sink);

} // namespace quic
