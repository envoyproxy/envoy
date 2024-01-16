#pragma once

#include "envoy/http/header_validator.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread.h"

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
struct CodecStats : public ::Envoy::Http::HeaderValidatorStats {
  using AtomicPtr = Thread::AtomicPtr<CodecStats, Thread::AtomicPtrAllocMode::DeleteOnDestruct>;

  CodecStats(ALL_HTTP1_CODEC_STATS(GENERATE_CONSTRUCTOR_COUNTER_PARAM)...)
      : ::Envoy::Http::HeaderValidatorStats()
            ALL_HTTP1_CODEC_STATS(GENERATE_CONSTRUCTOR_INIT_LIST) {}

  static CodecStats& atomicGet(AtomicPtr& ptr, Stats::Scope& scope) {
    return *ptr.get([&scope]() -> CodecStats* {
      return new CodecStats{ALL_HTTP1_CODEC_STATS(POOL_COUNTER_PREFIX(scope, "http1."))};
    });
  }

  void incDroppedHeadersWithUnderscores() override { dropped_headers_with_underscores_.inc(); }
  void incRequestsRejectedWithUnderscoresInHeaders() override {
    requests_rejected_with_underscores_in_headers_.inc();
  }
  // TODO(yanavlasov): add corresponding counter for H/1 codec.
  void incMessagingError() override {}

  ALL_HTTP1_CODEC_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
