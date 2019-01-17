#pragma once

#include "absl/base/macros.h"

// NOLINT(namespace-envoy)

#define HTTP2_FALLTHROUGH_IMPL ABSL_FALLTHROUGH_INTENDED
#define HTTP2_DIE_IF_NULL_IMPL(ptr) ABSL_DIE_IF_NULL(ptr)

// TODO: implement
#define HTTP2_UNREACHABLE_IMPL() 0
