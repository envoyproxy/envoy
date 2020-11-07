#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/base/attributes.h"

#define QUIC_MUST_USE_RESULT_IMPL ABSL_MUST_USE_RESULT
#define QUIC_UNUSED_IMPL ABSL_ATTRIBUTE_UNUSED
#define QUIC_CONST_INIT_IMPL ABSL_CONST_INIT
