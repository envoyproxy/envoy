#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/http/header_map.h"

namespace quiche {

using QuicheLowerCaseStringImpl = Envoy::Http::LowerCaseString;

}
