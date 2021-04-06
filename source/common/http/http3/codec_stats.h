#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/thread.h"

namespace Envoy {
namespace Http {
namespace Http3 {

/**
 * All stats for the HTTP/3 codec. @see stats_macros.h
 * TODO(danzh) populate all of them in codec.
 */
#define ALL_HTTP3_CODEC_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(dropped_headers_with_underscores)                                                        \
  COUNTER(header_overflow)                                                                         \
  COUNTER(requests_rejected_with_underscores_in_headers)                                           \
  COUNTER(rx_messaging_error)                                                                      \
  COUNTER(rx_reset)                                                                                \
  COUNTER(trailers)                                                                                \
  COUNTER(tx_reset)                                                                                \
  GAUGE(streams_active, Accumulate)

/**
 * Wrapper struct for the HTTP/3 codec stats. @see stats_macros.h
 */
struct CodecStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http3."),
                                                  POOL_GAUGE_PREFIX(scope, "http3."))};
    });
  }

  ALL_HTTP3_CODEC_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Http3
} // namespace Http
} // namespace Envoy
