#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// Link in actually implementation under //test. This is necessary because test
// only feature should stay under //test to for maintenance purpose.
#include "test/extensions/quic_listeners/quiche/platform/quic_port_utils_test_impl.h"
