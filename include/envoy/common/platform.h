#pragma once

// NOLINT(namespace-envoy)
#ifdef _MSC_VER
#include <stdint.h>

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

using ssize_t = ptrdiff_t;

#else
#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#endif
