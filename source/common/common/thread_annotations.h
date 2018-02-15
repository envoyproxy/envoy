#pragma once

// NOLINT(namespace-envoy)

#include "absl/base/thread_annotations.h"

#ifdef __APPLE__
#undef THREAD_ANNOTATION_ATTRIBUTE__
// See #2571, we get problematic warnings on Clang + OS X.
#define THREAD_ANNOTATION_ATTRIBUTE__(x) // no-op
#endif
