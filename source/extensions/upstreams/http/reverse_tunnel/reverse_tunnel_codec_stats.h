#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

/**
 * Stats for the drain-aware reverse-tunnel upstream (client) codec.
 */
#define ALL_REVERSE_TUNNEL_UPSTREAM_CODEC_STATS(COUNTER)                                           \
  COUNTER(goaway_received)                                                                         \
  COUNTER(goaway_sent)

struct ReverseTunnelUpstreamCodecStats {
  ALL_REVERSE_TUNNEL_UPSTREAM_CODEC_STATS(GENERATE_COUNTER_STRUCT)

  static ReverseTunnelUpstreamCodecStats generate(Stats::Scope& scope) {
    return {ALL_REVERSE_TUNNEL_UPSTREAM_CODEC_STATS(
        POOL_COUNTER_PREFIX(scope, "reverse_tunnel_upstream_codec."))};
  }
};

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
