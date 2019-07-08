#pragma once

// NOLINT(namespace-envoy)
#ifdef _MSC_VER
#include <stdint.h>

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

#ifdef _M_X64
using ssize_t = int64_t;
#else
#error Envoy is not supported on 32-bit Windows
#endif

#else
#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#endif
