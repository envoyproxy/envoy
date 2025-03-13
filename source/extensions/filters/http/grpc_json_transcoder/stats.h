#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

/**
 * All thrift filter stats. @see stats_macros.h
 */
#define ALL_GRPC_JSON_TRANSCODER_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                           \
  GAUGE(transcoder_request_buffer_bytes, Accumulate)                                               \
  GAUGE(transcoder_response_buffer_bytes, Accumulate)

/**
 * Struct definition for all grpc json transcoder filter stats. @see stats_macros.h
 */
struct GrpcJsonTranscoderFilterStats {
  ALL_GRPC_JSON_TRANSCODER_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                                        GENERATE_HISTOGRAM_STRUCT)

  static GrpcJsonTranscoderFilterStats generateStats(const std::string& prefix,
                                                     Stats::Scope& scope) {
    return GrpcJsonTranscoderFilterStats{ALL_GRPC_JSON_TRANSCODER_FILTER_STATS(
        POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix),
        POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

using GrpcJsonTranscoderFilterStatsSharedPtr = std::shared_ptr<GrpcJsonTranscoderFilterStats>;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
