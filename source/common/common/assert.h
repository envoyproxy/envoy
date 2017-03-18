#pragma once

#include "logger.h"

/**
 * assert macro that uses our builtin logging which gives us thread ID and can log to various
 * sinks.
 */
#define RELEASE_ASSERT(X)                                                                          \
  {                                                                                                \
    if (!(X)) {                                                                                    \
      Logger::Registry::getLog(Logger::Id::assert)                                                 \
          .critical("assert failure: {}: {}:{}", #X, __FILE__, __LINE__);                          \
      abort();                                                                                     \
    }                                                                                              \
  }

#ifndef NDEBUG
#define ASSERT(X) RELEASE_ASSERT(X)
#else
#define ASSERT(X)
#endif

/**
 * Indicate a panic situation and exit.
 */
#define PANIC(X)                                                                                   \
  Logger::Registry::getLog(Logger::Id::assert)                                                     \
      .critical("panic: {}: {}:{}", X, __FILE__, __LINE__);                                        \
  abort();

#define NOT_IMPLEMENTED PANIC("not implemented")

// NOT_REACHED is for spots the compiler insists on having a return, but where we know that it
// shouldn't be possible to arrive there, assuming no horrendous bugs. For example, after a
// switch (some_enum) with all enum values included in the cases.
#define NOT_REACHED PANIC("not reached")
