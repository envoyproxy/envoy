#pragma once

#include "common/common/logger.h"

namespace Envoy {

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
#define RELEASE_ASSERT(X, DETAILS)                                                                 \
  do {                                                                                             \
    if (!(X)) {                                                                                    \
      const std::string& details = (DETAILS);                                                      \
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,    \
                          "assert failure: {}.{}{}", #X,                                           \
                          details.empty() ? "" : " Details: ", details);                           \
      abort();                                                                                     \
    }                                                                                              \
  } while (false)

#ifndef NDEBUG
#define ASSERT(X) RELEASE_ASSERT(X, "")
#else
// This non-implementation ensures that its argument is a valid expression that can be statically
// casted to a bool, but the expression is never evaluated and will be compiled away.
#define ASSERT(X)                                                                                  \
  do {                                                                                             \
    constexpr bool __assert_dummy_variable = false && static_cast<bool>(X);                        \
    (void)__assert_dummy_variable;                                                                 \
  } while (false)
#endif

/**
 * Indicate a panic situation and exit.
 */
#define PANIC(X)                                                                                   \
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,        \
                      "panic: {}", X);                                                             \
  abort();

// NOT_IMPLEMENTED_GCOVR_EXCL_LINE is for overridden functions that are expressly not implemented.
// The macro name includes "GCOVR_EXCL_LINE" to exclude the macro's usage from code coverage
// reports.
#define NOT_IMPLEMENTED_GCOVR_EXCL_LINE PANIC("not implemented")

// NOT_REACHED_GCOVR_EXCL_LINE is for spots the compiler insists on having a return, but where we
// know that it shouldn't be possible to arrive there, assuming no horrendous bugs. For example,
// after a switch (some_enum) with all enum values included in the cases. The macro name includes
// "GCOVR_EXCL_LINE" to exclude the macro's usage from code coverage reports.
#define NOT_REACHED_GCOVR_EXCL_LINE PANIC("not reached")
} // Envoy
