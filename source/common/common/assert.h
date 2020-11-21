#pragma once

#include <functional>

#include "common/common/logger.h"

namespace Envoy {
namespace Assert {

class ActionRegistration {
public:
  virtual ~ActionRegistration() = default;
};
using ActionRegistrationPtr = std::unique_ptr<ActionRegistration>;

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
ActionRegistrationPtr setDebugAssertionFailureRecordAction(const std::function<void()>& action);

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
ActionRegistrationPtr setEnvoyBugFailureRecordAction(const std::function<void()>& action);

/**
 * Invokes the action set by setDebugAssertionFailureRecordAction, or does nothing if
 * no action has been set.
 *
 * This should only be called by ASSERT macros in this file.
 */
void invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly();

/**
 * Invokes the action set by setEnvoyBugFailureRecordAction, or does nothing if
 * no action has been set.
 *
 * This should only be called by ENVOY_BUG macros in this file.
 */
void invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly();

/**
 * Increments power of two counter for EnvoyBugRegistrationImpl.
 *
 * This should only be called by ENVOY_BUG macros in this file.
 */
bool shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(absl::string_view bug_name);

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
#define RELEASE_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, abort(), DETAILS)

/**
 * Assert macro intended for security guarantees. It has the same functionality
 * as RELEASE_ASSERT, but is intended for memory bounds-checking.
 */
#define SECURITY_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, abort(), DETAILS)

#if !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)

#if !defined(NDEBUG) // If this is a debug build.
#define ASSERT_ACTION abort()
#else // If this is not a debug build, but ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE is defined.
#define ASSERT_ACTION Envoy::Assert::invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly()
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
#define KNOWN_ISSUE_ASSERT(X, DETAILS) _ASSERT_IMPL(X, #X, abort(), DETAILS)
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
#else
#define ASSERT _NULL_ASSERT_IMPL
#define KNOWN_ISSUE_ASSERT _NULL_ASSERT_IMPL
#endif // !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)

/**
 * Indicate a panic situation and exit.
 */
#define PANIC(X)                                                                                   \
  do {                                                                                             \
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,      \
                        "panic: {}", X);                                                           \
    abort();                                                                                       \
  } while (false)

#if !defined(NDEBUG)
#define ENVOY_BUG_ACTION abort()
#else
#define ENVOY_BUG_ACTION Envoy::Assert::invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly()
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
 */
#define ENVOY_BUG(...) PASS_ON(PASS_ON(_ENVOY_BUG_VERBOSE)(__VA_ARGS__))

// NOT_IMPLEMENTED_GCOVR_EXCL_LINE is for overridden functions that are expressly not implemented.
// The macro name includes "GCOVR_EXCL_LINE" to exclude the macro's usage from code coverage
// reports.
#define NOT_IMPLEMENTED_GCOVR_EXCL_LINE PANIC("not implemented")

// NOT_REACHED_GCOVR_EXCL_LINE is for spots the compiler insists on having a return, but where we
// know that it shouldn't be possible to arrive there, assuming no horrendous bugs. For example,
// after a switch (some_enum) with all enum values included in the cases. The macro name includes
// "GCOVR_EXCL_LINE" to exclude the macro's usage from code coverage reports.
#define NOT_REACHED_GCOVR_EXCL_LINE PANIC("not reached")
} // namespace Assert
} // namespace Envoy
