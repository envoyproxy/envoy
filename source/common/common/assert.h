#pragma once

#include "common/common/logger.h"

namespace Envoy {

/**
 * assert macro that uses our builtin logging which gives us thread ID and can log to various
 * sinks.
 *
 * The old style release assert is of the form
 * RELEASE_ASSERT(foo == bar);
 * where it will log stack traces and the failed conditional and crash if the
 * condition is not met.
 *
 * The new style of release assert is of the form
 * RELEASE_ASSERT(foo == bar, "reason foo should actually be bar");
 * where new uses of RELEASE_ASSERT are strongly encouraged to supply a verbose
 * explanation of what went wrong.
 */
#define _NO_DETAILS() ""
#define _PRINT_DETAILS(Y) "some details"

#define __DETAILS_SELECTOR(MAYBE_ARGUMENT, ASSERT_MACRO, ...) ASSERT_MACRO

#define RELEASE_ASSERT(X, ...)                                                                        \
  do {                                                                                             \
    if (!(X)) {                                                                                    \
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,    \
                          "assert failure: {}{}", #X,                                              \
                          __DETAILS_SELECTOR(__VA_ARGS__, _PRINT_DETAILS, _NO_DETAILS)(__VA_ARGS__));                                                       \
      abort();                                                                                     \
    }                                                                                              \
  } while (false)

#ifndef NDEBUG
#define ASSERT(X) RELEASE_ASSERT(X)
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

#define NOT_IMPLEMENTED PANIC("not implemented")

// NOT_REACHED is for spots the compiler insists on having a return, but where we know that it
// shouldn't be possible to arrive there, assuming no horrendous bugs. For example, after a
// switch (some_enum) with all enum values included in the cases.
#define NOT_REACHED PANIC("not reached")
} // Envoy
