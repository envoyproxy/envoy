#pragma once

#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
/**
 * These are helper macros for allocating "fixed" stats throughout the code base in a way that
 * is also easy to mock and test. The general flow looks like this:
 *
 * Define a block of stats like this:
 *   #define MY_COOL_STATS(COUNTER, GAUGE, HISTOGRAM)     \
 *     COUNTER(counter1)                                  \
 *     GAUGE(gauge1, mode)                                \
 *     HISTOGRAM(histogram1, unit)
 *     ...
 *
 * By convention, starting with #7083, we sort the lines of this macro block, so
 * all the counters are grouped together, then all the gauges, etc. We do not
 * use clang-format-on/off etc. "./tools/check_format.py fix" will take care of
 * lining up the backslashes.
 *
 * Now actually put these stats somewhere, usually as a member of a struct:
 *   struct MyCoolStats {
 *     MY_COOL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
 *   };
 *
 * Finally, when you want to actually instantiate the above struct using a Stats::Pool, you do:
 *   MyCoolStats stats{
 *     MY_COOL_STATS(POOL_COUNTER(...), POOL_GAUGE(...), POOL_HISTOGRAM(...))};
 */

// Fully-qualified for use in external callsites.
#define GENERATE_COUNTER_STRUCT(NAME) Envoy::Stats::Counter& NAME##_;
#define GENERATE_GAUGE_STRUCT(NAME, MODE) Envoy::Stats::Gauge& NAME##_;
#define GENERATE_HISTOGRAM_STRUCT(NAME, UNIT) Envoy::Stats::Histogram& NAME##_;

#define FINISH_STAT_DECL_(X) #X)),
#define FINISH_STAT_DECL_MODE_(X, MODE) #X), Envoy::Stats::Gauge::ImportMode::MODE),
#define FINISH_STAT_DECL_UNIT_(X, UNIT) #X), Envoy::Stats::Histogram::Unit::UNIT),

static inline std::string statPrefixJoin(absl::string_view prefix, absl::string_view token) {
  if (prefix.empty()) {
    return std::string(token);
  } else if (absl::EndsWith(prefix, ".")) {
    // TODO(jmarantz): eliminate this case -- remove all the trailing dots from prefixes.
    return absl::StrCat(prefix, token);
  }
  return absl::StrCat(prefix, ".", token);
}

#define POOL_COUNTER_PREFIX(POOL, PREFIX) (POOL).counter(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_
#define POOL_GAUGE_PREFIX(POOL, PREFIX) (POOL).gauge(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_MODE_
#define POOL_HISTOGRAM_PREFIX(POOL, PREFIX) (POOL).histogram(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_UNIT_

#define POOL_COUNTER(POOL) POOL_COUNTER_PREFIX(POOL, "")
#define POOL_GAUGE(POOL) POOL_GAUGE_PREFIX(POOL, "")
#define POOL_HISTOGRAM(POOL) POOL_HISTOGRAM_PREFIX(POOL, "")

#define NULL_STAT_DECL_(X) std::string(#X)),
#define NULL_STAT_DECL_IGNORE_MODE_(X, MODE) std::string(#X)),

#define NULL_POOL_GAUGE(POOL) (POOL).nullGauge(NULL_STAT_DECL_IGNORE_MODE_
} // namespace Envoy
