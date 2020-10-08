#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/thread.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * All stats for the HTTP/2 codec. @see stats_macros.h
 */
#define ALL_HTTP2_CODEC_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(dropped_headers_with_underscores)                                                        \
  COUNTER(header_overflow)                                                                         \
  COUNTER(headers_cb_no_stream)                                                                    \
  COUNTER(inbound_empty_frames_flood)                                                              \
  COUNTER(inbound_priority_frames_flood)                                                           \
  COUNTER(inbound_window_update_frames_flood)                                                      \
  COUNTER(outbound_control_flood)                                                                  \
  COUNTER(outbound_flood)                                                                          \
  COUNTER(requests_rejected_with_underscores_in_headers)                                           \
  COUNTER(rx_messaging_error)                                                                      \
  COUNTER(rx_reset)                                                                                \
  COUNTER(trailers)                                                                                \
  COUNTER(tx_flush_timeout)                                                                        \
  COUNTER(tx_reset)                                                                                \
  COUNTER(keepalive_timeout)                                                                       \
  GAUGE(streams_active, Accumulate)                                                                \
  GAUGE(pending_send_bytes, Accumulate)

/**
 * Wrapper struct for the HTTP/2 codec stats. @see stats_macros.h
 */
struct CodecStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP2_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http2."),
                                                  POOL_GAUGE_PREFIX(scope, "http2."))};
    });
  }

  ALL_HTTP2_CODEC_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
