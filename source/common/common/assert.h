#pragma once

#include <functional>

#include "source/common/common/logger.h"

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

namespace Envoy {
namespace Assert {

class ActionRegistration {
public:
  virtual ~ActionRegistration() = default;
};
using ActionRegistrationPtr = std::unique_ptr<ActionRegistration>;

/*
 * EnvoyBugStackTrace captures and writes the stack trace to Envoy Bug
 * to assist with getting additional context for reports.
 */
class EnvoyBugStackTrace : private Logger::Loggable<Logger::Id::envoy_bug> {
public:
  EnvoyBugStackTrace() = default;
  /*
   * Capture the stack trace.
   * Skip count is one as to skip the last call which is capture().
   */
  void capture() {
    stack_depth_ = absl::GetStackTrace(stack_trace_, kMaxStackDepth, /* skip_count = */ 1);
  }

  /*
   * Logs each row of the captured stack into the envoy_bug log.
   */
  void logStackTrace() {
    ENVOY_LOG(error, "stacktrace for envoy bug");
    char out[1024];
    for (int i = 0; i < stack_depth_; ++i) {
      const bool success = absl::Symbolize(stack_trace_[i], out, sizeof(out));
      if (success) {
        ENVOY_LOG(error, "#{} {} [{}]", i, out, stack_trace_[i]);
      } else {
        ENVOY_LOG(error, "#{} {} [{}]", i, "UNKNOWN", stack_trace_[i]);
      }
    }
  }

private:
  static const int kMaxStackDepth = 16;
  void* stack_trace_[kMaxStackDepth];
  int stack_depth_{0};
};

/**
 * Sets an action to be invoked when a debug assertion failure is detected
 * in a release build. This action will be invoked each time an assertion
 * failure is detected.
 *
 * This function is not thread-safe; concurrent calls to set the action are not allowed.
 *
 * The action may be invoked concurrently if two ASSERTS in different threads fail at the
 * same time, so the action must be thread-safe.
 *
 * This has no effect in debug builds (assertion failure aborts the process)
 * or in release builds without ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE defined (assertion
 * tests are compiled out).
 *
 * @param action The action to take when an assertion fails.
 * @return A registration object. The registration is removed when the object is destructed.
 */
ActionRegistrationPtr
addDebugAssertionFailureRecordAction(const std::function<void(const char* location)>& action);

/**
 * Sets an action to be invoked when an ENVOY_BUG failure is detected in a release build. This
 * action will be invoked each time an ENVOY_BUG failure is detected.
 *
 * This function is not thread-safe; concurrent calls to set the action are not allowed.
 *
 * The action may be invoked concurrently if two ENVOY_BUGs in different threads fail at the
 * same time, so the action must be thread-safe.
 *
 * This has no effect in debug builds (envoy bug failure aborts the process).
 *
 * @param action The action to take when an envoy bug fails.
 * @return A registration object. The registration is removed when the object is destructed.
 */
ActionRegistrationPtr
addEnvoyBugFailureRecordAction(const std::function<void(const char* location)>& action);

/**
 * Invokes the action set by setDebugAssertionFailureRecordAction, or does nothing if
 * no action has been set.
 *
 * @param location Unique identifier for the ASSERT, currently source file and line.
 *
 * This should only be called by ASSERT macros in this file.
 */
void invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly(const char* location);

/**
 * Invokes the action set by setEnvoyBugFailureRecordAction, or does nothing if
 * no action has been set.
 *
 * @param location Unique identifier for the ENVOY_BUG, currently source file and line.
 *
 * This should only be called by ENVOY_BUG macros in this file.
 */
void invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly(const char* location);

/**
 * Increments power of two counter for EnvoyBugRegistrationImpl.
 *
 * @param bug_name Unique identifier for the ENVOY_BUG, currently source file and line.
 * @return True if the hit count is equal to a power of two after increment.
 *
 * This should only be called by ENVOY_BUG macros in this file.
 */
bool shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(absl::string_view bug_name);

/**
 * Resets all counters for EnvoyBugRegistrationImpl between tests.
 *
 */
void resetEnvoyBugCountersForTest();

// CONDITION_STR is needed to prevent macros in condition from being expected, which obfuscates
// the logged failure, e.g., "EAGAIN" vs "11".
#define _ASSERT_IMPL(CONDITION, CONDITION_STR, ACTION, DETAILS)                                    \
  do {                                                                                             \
    if (!(CONDITION)) {                                                                            \
      const std::string& details = (DETAILS);                                                      \
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,    \
                          "assert failure: {}.{}{}", CONDITION_STR,                                \
                          details.empty() ? "" : " Details: ", details);                           \
      ACTION;                                                                                      \
    }                                                                                              \
  } while (false)

// This non-implementation ensures that its argument is a valid expression that can be statically
// casted to a bool, but the expression is never evaluated and will be compiled away.
#define _NULL_ASSERT_IMPL(X, ...)                                                                  \
  do {                                                                                             \
    constexpr bool __assert_dummy_variable = false && static_cast<bool>(X);                        \
    (void)__assert_dummy_variable;                                                                 \
  } while (false)

/**
 * assert macro that uses our builtin logging which gives us thread ID and can log to various
 * sinks.
 *
 * The old style release assert was of the form RELEASE_ASSERT(foo == bar);
 * where it would log stack traces and the failed conditional and crash if the
 * condition is not met. The are many legacy RELEASE_ASSERTS in Envoy which
 * were converted to RELEASE_ASSERT(foo == bar, "");
 *
 * The new style of release assert is of the form
 * RELEASE_ASSERT(foo == bar, "reason foo should actually be bar");
 * new uses of RELEASE_ASSERT should supply a verbose explanation of what went wrong.
 */
#define RELEASE_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, ::abort(), DETAILS)

/**
 * Assert macro intended for Envoy Mobile. It creates enforcement for mobile
 * clients but has no effect for Envoy as a server.
 */
#if TARGET_OS_IOS || defined(__ANDROID_API__)
#define MOBILE_RELEASE_ASSERT(X, DETAILS) RELEASE_ASSERT(X, DETAILS)
#else
#define MOBILE_RELEASE_ASSERT(X, DETAILS)
#endif

/**
 * Assert macro intended for security guarantees. It has the same functionality
 * as RELEASE_ASSERT, but is intended for memory bounds-checking.
 */
#define SECURITY_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, ::abort(), DETAILS)

// ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE compiles all ASSERTs in release mode.
#ifdef ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE
#define ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE
#endif

#if !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE) ||                              \
    defined(ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE)
// This if condition represents any case where ASSERT()s are compiled in.

#if !defined(NDEBUG) // If this is a debug build.
#define ASSERT_ACTION ::abort()
#else // If this is not a debug build, but ENVOY_LOG_(FAST)_DEBUG_ASSERT_IN_RELEASE is defined.
#define ASSERT_ACTION                                                                              \
  Envoy::Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly(                     \
      __FILE__ ":" TOSTRING(__LINE__))
#endif // !defined(NDEBUG)

#define _ASSERT_ORIGINAL(X) _ASSERT_IMPL(X, #X, ASSERT_ACTION, "")
#define _ASSERT_VERBOSE(X, Y) _ASSERT_IMPL(X, #X, ASSERT_ACTION, Y)
#define _ASSERT_SELECTOR(_1, _2, ASSERT_MACRO, ...) ASSERT_MACRO

// This is a workaround for fact that MSVC expands __VA_ARGS__ after passing them into a macro,
// rather than before passing them into a macro. Without this, _ASSERT_SELECTOR does not work
// correctly when compiled with MSVC
#define EXPAND(X) X

#if !defined(ENVOY_DISABLE_KNOWN_ISSUE_ASSERTS)
/**
 * Assert wrapper for an as-yet unidentified issue. Even with ASSERTs compiled in, it may be
 * excluded, by defining ENVOY_DISABLE_KNOWN_ISSUE_ASSERTS. It represents a condition that
 * should always pass but that sometimes fails for an unknown reason. The macro allows it to
 * be temporarily compiled out while the failure is triaged and investigated.
 */
#define KNOWN_ISSUE_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, ::abort(), DETAILS)
#else
// This non-implementation ensures that its argument is a valid expression that can be statically
// casted to a bool, but the expression is never evaluated and will be compiled away.
#define KNOWN_ISSUE_ASSERT _NULL_ASSERT_IMPL
#endif // defined(ENVOY_DISABLE_KNOWN_ISSUE_ASSERTS)

// If ASSERT is called with one argument, the ASSERT_SELECTOR will return
// _ASSERT_ORIGINAL and this will call _ASSERT_ORIGINAL(__VA_ARGS__).
// If ASSERT is called with two arguments, ASSERT_SELECTOR will return
// _ASSERT_VERBOSE, and this will call _ASSERT_VERBOSE,(__VA_ARGS__)
#define ASSERT(...)                                                                                \
  EXPAND(_ASSERT_SELECTOR(__VA_ARGS__, _ASSERT_VERBOSE, _ASSERT_ORIGINAL)(__VA_ARGS__))

#if !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
// debug build or all ASSERTs compiled in release.
#define SLOW_ASSERT(...) ASSERT(__VA_ARGS__)
#else
// Non-implementation of SLOW_ASSERTs when building only ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE.
#define SLOW_ASSERT _NULL_ASSERT_IMPL
#endif // !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)

#else
#define ASSERT _NULL_ASSERT_IMPL
#define KNOWN_ISSUE_ASSERT _NULL_ASSERT_IMPL
#define SLOW_ASSERT _NULL_ASSERT_IMPL
#endif // !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE) ||
       // defined(ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE)

/**
 * Indicate a panic situation and exit.
 */
#define PANIC(X)                                                                                   \
  do {                                                                                             \
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,      \
                        "panic: {}", X);                                                           \
    ::abort();                                                                                     \
  } while (false)

// We do not want to crash on failure in tests exercising ENVOY_BUGs while running coverage in debug
// mode. Crashing causes flakes when forking to expect a debug death and reduces lines of coverage.
#if !defined(NDEBUG) && !defined(ENVOY_CONFIG_COVERAGE)
#define ENVOY_BUG_ACTION ::abort()
#else
#define ENVOY_BUG_ACTION                                                                           \
  Envoy::Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly(__FILE__                 \
                                                                          ":" TOSTRING(__LINE__))
#endif

// These macros are needed to stringify __LINE__ correctly.
#define STRINGIFY(X) #X
#define TOSTRING(X) STRINGIFY(X)

// CONDITION_STR is needed to prevent macros in condition from being expected, which obfuscates
// the logged failure, e.g., "EAGAIN" vs "11".
// ENVOY_BUG logging and actions are invoked only on power-of-two instances per log line.
#define _ENVOY_BUG_IMPL(CONDITION, CONDITION_STR, ACTION, DETAILS)                                 \
  do {                                                                                             \
    if (!(CONDITION) && Envoy::Assert::shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(          \
                            __FILE__ ":" TOSTRING(__LINE__))) {                                    \
      const std::string& details = (DETAILS);                                                      \
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::envoy_bug), error,    \
                          "envoy bug failure: {}.{}{}", CONDITION_STR,                             \
                          details.empty() ? "" : " Details: ", details);                           \
      Envoy::Assert::EnvoyBugStackTrace st;                                                        \
      st.capture();                                                                                \
      st.logStackTrace();                                                                          \
      ACTION;                                                                                      \
    }                                                                                              \
  } while (false)

#define _ENVOY_BUG_VERBOSE(X, Y) _ENVOY_BUG_IMPL(X, #X, ENVOY_BUG_ACTION, Y)

// This macro is needed to help to remove: "warning C4003: not enough arguments for function-like
// macro invocation '<identifier>'" when expanding __VA_ARGS__. In our setup, MSVC treats this
// warning as an error. A sample code to reproduce the case: https://godbolt.org/z/M4zZNG.
#define PASS_ON(...) __VA_ARGS__

/**
 * Indicate a failure condition that should never be met in normal circumstances. In contrast
 * with ASSERT, an ENVOY_BUG is compiled in release mode. If a failure condition is met in release
 * mode, it is logged and a stat is incremented with exponential back-off per ENVOY_BUG. In debug
 * mode, it will crash if the condition is not met. ENVOY_BUG must be called with two arguments for
 * verbose logging.
 * Note: ENVOY_BUGs in coverage mode will never crash. They will log and increment a stat like in
 * release mode. This prevents flakiness and increases code coverage.
 */
#define ENVOY_BUG(...) PASS_ON(PASS_ON(_ENVOY_BUG_VERBOSE)(__VA_ARGS__))

// Always triggers ENVOY_BUG. This is intended for paths that are not expected to be reached.
#define IS_ENVOY_BUG(...) ENVOY_BUG(false, __VA_ARGS__);

// It is safer to avoid defaults in switch statements, so that as new enums are added, the compiler
// checks that new code is added as well. Google's proto library adds 2 sentinel values which should
// not be used, and this macro allows avoiding using "default:" to handle them.
#define PANIC_ON_PROTO_ENUM_SENTINEL_VALUES                                                        \
  case std::numeric_limits<int32_t>::max():                                                        \
    FALLTHRU;                                                                                      \
  case std::numeric_limits<int32_t>::min():                                                        \
    PANIC("unexpected sentinel value used")

#define PANIC_DUE_TO_PROTO_UNSET PANIC("unset oneof")

// Envoy has a number of switch statements which panic if there's no legal value set.
// This is not encouraged, as it's too easy to panic using break; instead of return;
// but this macro replaces a less clear crash using NOT_REACHED_GCOVR_EXCL_LINE.
#define PANIC_DUE_TO_CORRUPT_ENUM PANIC("corrupted enum");

} // namespace Assert
} // namespace Envoy
