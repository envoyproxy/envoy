#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/base/attributes.h"

// These macros are documented in: quiche/quic/platform/api/quic_export.h

#if defined(_WIN32)
#define QUICHE_EXPORT_IMPL
#elif ABSL_HAVE_ATTRIBUTE(visibility)
#define QUICHE_EXPORT_IMPL __attribute__((visibility("default")))
#else
#define QUICHE_EXPORT_IMPL
#endif

#define QUICHE_NO_EXPORT_IMPL QUICHE_EXPORT_IMPL
