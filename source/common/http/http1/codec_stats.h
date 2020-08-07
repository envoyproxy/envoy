#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/thread.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * All stats for the HTTP/1 codec. @see stats_macros.h
 */
#define ALL_HTTP1_CODEC_STATS(COUNTER)                                                             \
  COUNTER(dropped_headers_with_underscores)                                                        \
  COUNTER(metadata_not_supported_error)                                                            \
  COUNTER(requests_rejected_with_underscores_in_headers)                                           \
  COUNTER(response_flood)

/**
 * Wrapper struct for the HTTP/1 codec stats. @see stats_macros.h
 */
struct CodecStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP1_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http1."))};
    });
  }

  ALL_HTTP1_CODEC_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
