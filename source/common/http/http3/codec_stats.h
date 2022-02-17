#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread.h"

namespace Envoy {
namespace Http {
namespace Http3 {

/**
 * All stats for the HTTP/3 codec. @see stats_macros.h
 */
#define ALL_HTTP3_CODEC_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(dropped_headers_with_underscores)                                                        \
  COUNTER(requests_rejected_with_underscores_in_headers)                                           \
  COUNTER(rx_reset)                                                                                \
  COUNTER(tx_reset)                                                                                \
  COUNTER(metadata_not_supported_error)                                                            \
  COUNTER(quic_version_h3_29)                                                                      \
  COUNTER(quic_version_rfc_v1)                                                                     \
  COUNTER(tx_flush_timeout)

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
