#pragma once

#include "envoy/http/header_validator.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * All stats for the HTTP/2 codec. @see stats_macros.h
 */
#define ALL_HTTP2_CODEC_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(dropped_headers_with_underscores)                                                        \
  COUNTER(goaway_sent)                                                                             \
  COUNTER(header_overflow)                                                                         \
  COUNTER(headers_cb_no_stream)                                                                    \
  COUNTER(inbound_empty_frames_flood)                                                              \
  COUNTER(inbound_priority_frames_flood)                                                           \
  COUNTER(inbound_window_update_frames_flood)                                                      \
  COUNTER(keepalive_timeout)                                                                       \
  COUNTER(metadata_empty_frames)                                                                   \
  COUNTER(outbound_control_flood)                                                                  \
  COUNTER(outbound_flood)                                                                          \
  COUNTER(requests_rejected_with_underscores_in_headers)                                           \
  COUNTER(rx_messaging_error)                                                                      \
  COUNTER(rx_reset)                                                                                \
  COUNTER(stream_refused_errors)                                                                   \
  COUNTER(trailers)                                                                                \
  COUNTER(tx_flush_timeout)                                                                        \
  COUNTER(tx_reset)                                                                                \
  GAUGE(streams_active, Accumulate)                                                                \
  GAUGE(pending_send_bytes, Accumulate)                                                            \
  GAUGE(deferred_stream_close, Accumulate)                                                         \
  GAUGE(outbound_frames_active, Accumulate)                                                        \
  GAUGE(outbound_control_frames_active, Accumulate)
/**
 * Wrapper struct for the HTTP/2 codec stats. @see stats_macros.h
 */
struct CodecStats : public ::Envoy::Http::HeaderValidatorStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  CodecStats(ALL_HTTP2_CODEC_STATS(GENERATE_CONSTRUCTOR_COUNTER_PARAM,
                                   GENERATE_CONSTRUCTOR_GAUGE_PARAM)...)
      : ::Envoy::Http::HeaderValidatorStats()
            ALL_HTTP2_CODEC_STATS(GENERATE_CONSTRUCTOR_INIT_LIST, GENERATE_CONSTRUCTOR_INIT_LIST) {}

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP2_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http2."),
                                                  POOL_GAUGE_PREFIX(scope, "http2."))};
    });
  }

  void incDroppedHeadersWithUnderscores() override { dropped_headers_with_underscores_.inc(); }
  void incRequestsRejectedWithUnderscoresInHeaders() override {
    requests_rejected_with_underscores_in_headers_.inc();
  }
  void incMessagingError() override { rx_messaging_error_.inc(); }

  ALL_HTTP2_CODEC_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
