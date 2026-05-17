#pragma once

// This header is a placeholder which we will carry until June 1, 2026,
// as we have deprecated the pure interface and impl pattern.
//
// Please remove references to this file and instead include
// source/common/stats/allocator.h directly.

#include "source/common/stats/allocator.h"

namespace Envoy {
namespace Stats {

// This alias is provided for out-of-repository includes of allocator_impl.h,
// who will be expecting the concrete class to be called AllocatorImpl.
using AllocatorImpl = Allocator;

} // namespace Stats
} // namespace Envoy
