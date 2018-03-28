#pragma once

#include "common/common/logger.h"

namespace Envoy {
/**
 * assert macro that uses our builtin logging which gives us thread ID and can log to various
 * sinks.
 */
#define RELEASE_ASSERT(X)                                                                          \
  do {                                                                                             \
    if (!(X)) {                                                                                    \
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::assert), critical,    \
                          "assert failure: {}", #X);                                               \
      abort();                                                                                     \
    }                                                                                              \
  } while (false)

#ifndef NDEBUG
#define ASSERT(X) RELEASE_ASSERT(X)
#else
// This non-implementation ensures that its argument is a valid expression that can be statically
// casted to a bool, but doesn't generate any code, since the argument to sizeof() is not evaluated.
#define ASSERT(X)                                                                                  \
  do {                                                                                             \
    (void)sizeof(static_cast<bool>(X));                                                            \
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
