#pragma once

// NOLINT(namespace-envoy)
#if !defined(WIN32)
#define STACK_ALLOC_ARRAY(var, type, num) type var[num]

#define PACKED_STRUCT(definition, name) typedef definition __attribute__((packed)) name

#else
#include <malloc.h>

#define STACK_ALLOC_ARRAY(var, type, num)                                                          \
  type* var = static_cast<type*>(::_alloca(sizeof(type) * num))

#define PACKED_STRUCT(definition, name)                                                            \
  __pragma(pack(push, 1)) typedef definition name;                                                 \
  __pragma(pack(pop))

#endif
