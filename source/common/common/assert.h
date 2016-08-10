#pragma once

#include "logger.h"

/**
 * assert macro that uses our builtin logging which gives us thread ID and can log to various
 * sinks.
 */
#ifndef COVERAGE
#define RELEASE_ASSERT(X)                                                                          \
  {                                                                                                \
    if (!(X)) {                                                                                    \
      Logger::Registry::getLog(Logger::Id::assert)                                                 \
          .emerg("assert failure: {}: {}:{}", #X, __FILE__, __LINE__);                             \
      abort();                                                                                     \
    }                                                                                              \
  }
#else
#define RELEASE_ASSERT(X)
#endif

#ifndef NDEBUG
#define ASSERT(X) RELEASE_ASSERT(X)
#else
#define ASSERT(X)
#endif

/**
 * Indicate a panic situation and exit.
 */
#define PANIC(X)                                                                                   \
  Logger::Registry::getLog(Logger::Id::assert).emerg("panic: {}: {}:{}", X, __FILE__, __LINE__);   \
  abort();

#define NOT_IMPLEMENTED PANIC("not implemented")
