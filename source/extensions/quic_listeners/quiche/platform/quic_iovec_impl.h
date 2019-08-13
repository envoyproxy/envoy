#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// TODO(danzh) Add Windows support for iovec.
// Only works in platforms supports POSIX for now.
#include <sys/uio.h>
