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

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

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
      quiche::QuicheLogEmitter(static_cast<quiche::QuicheLogLevel>(quiche::LogLevel##severity),    \
                               __FILE__, __LINE__, __func__)                                       \
          .stream())

#define QUICHE_LOG_IMPL(severity) QUICHE_LOG_IF_IMPL(severity, true)

#define QUICHE_VLOG_IF_IMPL(verbosity, condition)                                                  \
  QUICHE_LOG_IMPL_INTERNAL(                                                                        \
      quiche::isVerboseLogEnabled(verbosity) && (condition),                                       \
      quiche::QuicheLogEmitter(static_cast<quiche::QuicheLogLevel>(quiche::LogLevelINFO),          \
                               __FILE__, __LINE__, __func__)                                       \
          .stream())

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
      quiche::QuicheLogEmitter(static_cast<quiche::QuicheLogLevel>(quiche::LogLevel##severity),    \
                               __FILE__, __LINE__, __func__)                                       \
          .SetPerror()                                                                             \
          .stream())

#define QUICHE_LOG_INFO_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(INFO)
#define QUICHE_LOG_WARNING_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(WARNING)
#define QUICHE_LOG_ERROR_IS_ON_IMPL() QUICHE_IS_LOG_LEVEL_ENABLED(ERROR)

#define QUICHE_CHECK_INNER_IMPL(condition, details)                                                \
  QUICHE_LOG_IF_IMPL(FATAL, ABSL_PREDICT_FALSE(!(condition))) << details << ". "

#define QUICHE_CHECK_IMPL(condition) QUICHE_CHECK_INNER_IMPL(condition, "Check failed: " #condition)

#define QUICHE_CHECK_INNER_IMPL_OP(op, a, b)                                                       \
  QUICHE_CHECK_INNER_IMPL((a)op(b),                                                                \
                          "Check failed: " #a " (=" << (a) << ") " #op " " #b " (=" << (b) << ")")

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
  QUICHE_LOG_IMPL_INTERNAL(false && (condition), quiche::NullLogStream().stream())
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
#define QUICHE_NOTREACHED_IMPL() PANIC("reached unexpected code")
#define QUICHE_DCHECK_GT_IMPL(a, b) QUICHE_CHECK_GT_IMPL(a, b)
#define QUICHE_DCHECK_GE_IMPL(a, b) QUICHE_CHECK_GE_IMPL(a, b)
#define QUICHE_DCHECK_LT_IMPL(a, b) QUICHE_CHECK_LT_IMPL(a, b)
#define QUICHE_DCHECK_LE_IMPL(a, b) QUICHE_CHECK_LE_IMPL(a, b)
#define QUICHE_DCHECK_NE_IMPL(a, b) QUICHE_CHECK_NE_IMPL(a, b)
#define QUICHE_DCHECK_EQ_IMPL(a, b) QUICHE_CHECK_EQ_IMPL(a, b)
#endif

#define QUICHE_PREDICT_FALSE_IMPL(x) ABSL_PREDICT_FALSE(x)
#define QUICHE_PREDICT_TRUE_IMPL(x) ABSL_PREDICT_TRUE(x)

namespace quiche {

// QuicheLogLevel and this enum exist to bridge the gap between QUICHE logging and Envoy's spdlog.
// QUICHE logs are used in forms such as QUICHE_LOG(ERROR) which is why the enum values have
// non-standard casing, as they are used in pre-processor token concatenation by macros in this
// file. We cannot use an enum class like quiche::LogLevel::DEBUG here because some of Envoy's build
// environments specify -DDEBUG=1 which would cause the enum value to be replaced by the
// pre-processor. This format of token avoids that issue.
enum {
  LogLevelTRACE = spdlog::level::trace,
  LogLevelDEBUG = spdlog::level::debug,
  LogLevelINFO = spdlog::level::info,
  LogLevelWARNING = spdlog::level::warn,
  LogLevelERROR = spdlog::level::err,
  LogLevelFATAL = spdlog::level::critical,
// DFATAL is FATAL in debug mode, ERROR in release mode.
#ifdef NDEBUG
  LogLevelDFATAL = LogLevelERROR,
#else  // NDEBUG
  LogLevelDFATAL = LogLevelFATAL,
#endif // NDEBUG
};

using QuicheLogLevel = spdlog::level::level_enum;

// NullGuard exists such that NullGuard<T>::guard(v) returns v, unless passed
// a nullptr_t, or a null char* or const char*, in which case it returns
// "(null)". This allows streaming NullGuard<T>::guard(v) to an output stream
// without hitting undefined behavior for null values.
template <typename T> struct NullGuard {
  static const T& guard(const T& v) { return v; }
};
template <> struct NullGuard<char*> {
  static const char* guard(const char* v) { return v ? v : "(null)"; }
};
template <> struct NullGuard<const char*> {
  static const char* guard(const char* v) { return v ? v : "(null)"; }
};
template <> struct NullGuard<std::nullptr_t> {
  static const char* guard(const std::nullptr_t&) { return "(null)"; }
};

class QuicheLogEmitter {
public:
  // |file_name| and |function_name| MUST be valid for the lifetime of the QuicheLogEmitter. This is
  // guaranteed when passing __FILE__ and __func__.
  explicit QuicheLogEmitter(QuicheLogLevel level, const char* file_name, int line,
                            const char* function_name);

  ~QuicheLogEmitter();

  // NOLINTNEXTLINE(readability-identifier-naming)
  QuicheLogEmitter& SetPerror() {
    is_perror_ = true;
    return *this;
  }

  template <typename T> QuicheLogEmitter& operator<<(const T& v) {
    stream_ << NullGuard<T>::guard(v);
    return *this;
  }

  // Handle stream manipulators such as std::endl.
  QuicheLogEmitter& operator<<(std::ostream& (*m)(std::ostream& os)) {
    stream_ << m;
    return *this;
  }
  QuicheLogEmitter& operator<<(std::ios_base& (*m)(std::ios_base& os)) {
    stream_ << m;
    return *this;
  }

  QuicheLogEmitter& stream() { return *this; }

private:
  const QuicheLogLevel level_;
  const char* file_name_;
  int line_;
  const char* function_name_;
  const int saved_errno_;
  bool is_perror_ = false;
  std::ostringstream stream_;
};

class NullLogStream {
public:
  NullLogStream& stream() { return *this; }
};

template <typename T> inline NullLogStream& operator<<(NullLogStream& s, const T&) { return s; }
// Handle stream manipulators such as std::endl.
inline NullLogStream& operator<<(NullLogStream& s, std::ostream& (*)(std::ostream&)) { return s; }
inline NullLogStream& operator<<(NullLogStream& s, std::ios_base& (*)(std::ios_base&)) { return s; }

inline spdlog::logger& GetLogger() {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::quic);
}

// This allows us to use QUICHE_CHECK(condition) from constexpr functions.
#define QUICHE_IS_LOG_LEVEL_ENABLED(severity) quiche::isLogLevelEnabled##severity()
#define QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(severity)                                                 \
  inline bool isLogLevelEnabled##severity() {                                                      \
    return static_cast<spdlog::level::level_enum>(quiche::LogLevel##severity) >=                   \
           GetLogger().level();                                                                    \
  }
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(TRACE)
QUICHE_IS_LOG_LEVEL_ENABLED_IMPL(DEBUG)
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

// QuicheLogSink is used to capture logs emitted from the QUICHE_LOG... macros.
class QuicheLogSink {
public:
  virtual ~QuicheLogSink() = default;

  // Called when |message| is emitted at |level|.
  // NOLINTNEXTLINE(readability-identifier-naming)
  virtual void Log(QuicheLogLevel level, const std::string& message) = 0;
};

// Only one QuicheLogSink can capture log at a time. SetLogSink causes future logs
// to be captured by the |new_sink|.
// Return the previous sink.
// NOLINTNEXTLINE(readability-identifier-naming)
QuicheLogSink* SetLogSink(QuicheLogSink* new_sink);

} // namespace quiche
