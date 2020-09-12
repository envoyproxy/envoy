#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

#define SKYWALKING_TRACER_STATS(COUNTER)                                                           \
  COUNTER(segments_sent)                                                                           \
  COUNTER(segments_dropped)                                                                        \
  COUNTER(cache_flushed)                                                                           \
  COUNTER(segments_flushed)

struct SkyWalkingTracerStats {
  SKYWALKING_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
