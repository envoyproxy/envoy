#pragma once

#include "envoy/http/header_validator.h"
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
struct CodecStats : public ::Envoy::Http::HeaderValidatorStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  CodecStats(ALL_HTTP3_CODEC_STATS(GENERATE_CONSTRUCTOR_COUNTER_PARAM,
                                   GENERATE_CONSTRUCTOR_GAUGE_PARAM)...)
      : ::Envoy::Http::HeaderValidatorStats()
            ALL_HTTP3_CODEC_STATS(GENERATE_CONSTRUCTOR_INIT_LIST, GENERATE_CONSTRUCTOR_INIT_LIST) {}

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http3."),
                                                  POOL_GAUGE_PREFIX(scope, "http3."))};
    });
  }

  void incDroppedHeadersWithUnderscores() override { dropped_headers_with_underscores_.inc(); }
  void incRequestsRejectedWithUnderscoresInHeaders() override {
    requests_rejected_with_underscores_in_headers_.inc();
  }
  // TODO(yanavlasov): add corresponding counter for H/3 codec.
  void incMessagingError() override {}

  ALL_HTTP3_CODEC_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

} // namespace Http3
} // namespace Http
} // namespace Envoy
