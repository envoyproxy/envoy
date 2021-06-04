#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <memory>

#include "envoy/api/io_error.h"

#define QUIC_EMSGSIZE_IMPL static_cast<int>(Envoy::Api::IoError::IoErrorCode::MessageTooBig)
