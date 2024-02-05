#pragma once

#include "envoy/stats/primitive_stats.h"

#include "absl/strings/string_view.h"

namespace Envoy {
/**
 * These are helper macros for allocating "fixed" stats throughout the code base in a way that
 * is also easy to mock and test. The general flow looks like this:
 *
 * Define a block of stats like this:
 *   #define MY_COOL_STATS(COUNTER, GAUGE)     \
 *     COUNTER(counter1)                       \
 *     GAUGE(gauge1)                           \
 *     ...
 *
 * By convention, starting with #7083, we sort the lines of this macro block, so
 * all the counters are grouped together, then all the gauges, etc. We do not
 * use clang-format-on/off etc. "bazel run //tools/code_format:check_format -- fix" will take
 * care of lining up the backslashes.
 *
 * Now actually put these stats somewhere, usually as a member of a struct:
 *   struct MyCoolStats {
 *     MY_COOL_STATS(GENERATE_PRIMITIVE_COUNTER_STRUCT, GENERATE_PRIMITIVE_GAUGE_STRUCT);
 *
 *     // Optional: Provide access to counters as a map.
 *     std::vector<std::pair<absl::string_view, PrimitiveCounterReference>> counters() const {
 *       return {MY_COOL_STATS(PRIMITIVE_COUNTER_NAME_AND_REFERENCE, IGNORE_PRIMITIVE_GAUGE)};
 *     }
 *
 *     // Optional: Provide access to gauges as a map.
 *     std::vector<std::pair<absl::string_view, PrimitiveGaugeReference>> gauges() const {
 *       return {MY_COOL_STATS(IGNORE_PRIMITIVE_COUNTER, PRIMITIVE_GAUGE_NAME_AND_REFERENCE)};
 *     }
 *   };
 *
 * Finally, when you want to actually instantiate the above struct you do:
 *   MyCoolStats stats;
 */

// Fully-qualified for use in external callsites.
#define GENERATE_PRIMITIVE_COUNTER_STRUCT(NAME) Envoy::Stats::PrimitiveCounter NAME##_;
#define GENERATE_PRIMITIVE_GAUGE_STRUCT(NAME) Envoy::Stats::PrimitiveGauge NAME##_;

// Name and counter/gauge reference pair used to construct map of counters/gauges.
#define PRIMITIVE_COUNTER_NAME_AND_REFERENCE(X) {absl::string_view(#X), std::ref(X##_)},
#define PRIMITIVE_GAUGE_NAME_AND_REFERENCE(X) {absl::string_view(#X), std::ref(X##_)},

// Ignore a counter or gauge.
#define IGNORE_PRIMITIVE_COUNTER(X)
#define IGNORE_PRIMITIVE_GAUGE(X)

} // namespace Envoy
