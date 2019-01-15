#pragma once

// NOLINT(namespace-envoy)

// Macros that depend on the compiler
#ifdef _MSC_VER
#include <malloc.h>
#include <stdint.h>

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

#ifdef _M_AMD64
typedef int64_t ssize_t;
#elif _M_IX86
typedef int32_t ssize_t;
#else
#error add ssize_t typedef for Windows platform
#endif

#else // _MSC_VER
#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#endif
