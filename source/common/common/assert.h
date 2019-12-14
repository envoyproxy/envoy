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
 * Invokes the action set by setDebugAssertionFailureRecordAction, or does nothing if
 * no action has been set.
 *
 * This should only be called by ASSERT macros in this file.
 */
void invokeDebugAssertionFailureRecordAction_ForAssertMacroUseOnly();

// CONDITION_STR is needed to prevent macros in condition from being expected, which obfuscates
// the logged failure, eg "EAGAIN" vs "11".
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

#if !defined(NDEBUG) || defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)

#if !defined(NDEBUG) // If this is a debug build.
#define ASSERT_ACTION abort()
#else // If this is not a debug build, but ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE is defined.
#define ASSERT_ACTION Envoy::Assert::invokeDebugAssertionFailureRecordAction_ForAssertMacroUseOnly()
#endif // !defined(NDEBUG)

#define _ASSERT_ORIGINAL(X) _ASSERT_IMPL(X, #X, ASSERT_ACTION, "")
#define _ASSERT_VERBOSE(X, Y) _ASSERT_IMPL(X, #X, ASSERT_ACTION, Y)
#define _ASSERT_SELECTOR(_1, _2, ASSERT_MACRO, ...) ASSERT_MACRO

// This is a workaround for fact that MSVC expands __VA_ARGS__ after passing them into a macro,
// rather than before passing them into a macro. Without this, _ASSERT_SELECTOR does not work
// correctly when compiled with MSVC
#define EXPAND(X) X

// If ASSERT is called with one argument, the ASSERT_SELECTOR will return
// _ASSERT_ORIGINAL and this will call _ASSERT_ORIGINAL(__VA_ARGS__).
// If ASSERT is called with two arguments, ASSERT_SELECTOR will return
// _ASSERT_VERBOSE, and this will call _ASSERT_VERBOSE,(__VA_ARGS__)
#define ASSERT(...)                                                                                \
  EXPAND(_ASSERT_SELECTOR(__VA_ARGS__, _ASSERT_VERBOSE, _ASSERT_ORIGINAL)(__VA_ARGS__))
#else
// This non-implementation ensures that its argument is a valid expression that can be statically
// casted to a bool, but the expression is never evaluated and will be compiled away.
#define ASSERT(X, ...)                                                                             \
  do {                                                                                             \
    constexpr bool __assert_dummy_variable = false && static_cast<bool>(X);                        \
    (void)__assert_dummy_variable;                                                                 \
  } while (false)
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
