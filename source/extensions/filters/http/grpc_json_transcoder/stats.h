#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/prefix_utility.h"

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
    // The filter's stats sit directly under the "http.<hcm>." parent prefix (no filter-name
    // segment), so the whole prefix is the parent and the filter's own prefix is empty.
    Stats::TaggedStatName stat_prefix = Stats::mergeStatPrefix(scope.symbolTable(), prefix, "");
    return GrpcJsonTranscoderFilterStats{ALL_GRPC_JSON_TRANSCODER_FILTER_STATS(
        POOL_COUNTER_TAGGED(scope, stat_prefix), POOL_GAUGE_TAGGED(scope, stat_prefix),
        POOL_HISTOGRAM_TAGGED(scope, stat_prefix))};
  }
};

using GrpcJsonTranscoderFilterStatsSharedPtr = std::shared_ptr<GrpcJsonTranscoderFilterStats>;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
