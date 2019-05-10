#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/time/clock.h"

namespace epoll_server {

inline int64_t WallTimeNowInUsecImpl() { return absl::GetCurrentTimeNanos() / 1000; }

} // namespace epoll_server
