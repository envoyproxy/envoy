#pragma once

// NOLINT(namespace-envoy)
#if !defined(_MSC_VER)
#define STACK_ALLOC_ARRAY(var, type, num) type var[num]

#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#else
#include <malloc.h>

#define STACK_ALLOC_ARRAY(var, type, num)                                                          \
  type* var = static_cast<type*>(::_alloca(sizeof(type) * num))

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

#endif
