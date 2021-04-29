#pragma once

#include "common/common/assert.h"

// This is a minimal Envoy adaptation layer for the Chromium URL library.
// NOLINT(namespace-envoy)

#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                                         \
  TypeName(const TypeName&) = delete;                                                              \
  TypeName& operator=(const TypeName&) = delete

#define EXPORT_TEMPLATE_DECLARE(x)
#define EXPORT_TEMPLATE_DEFINE(x)
#define COMPONENT_EXPORT(x)

#define DCHECK(x) ASSERT(x)
#define NOTREACHED() NOT_REACHED_GCOVR_EXCL_LINE
