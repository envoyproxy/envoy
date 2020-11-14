#pragma once

#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "common/stats/symbol_table_impl.h"
#include "common/stats/utility.h"

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
 * use clang-format-on/off etc. "./tools/code_format/check_format.py fix" will take care of
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
#define GENERATE_TEXT_READOUT_STRUCT(NAME) Envoy::Stats::TextReadout& NAME##_;

#define FINISH_STAT_DECL_(X) #X)),
#define FINISH_STAT_DECL_MODE_(X, MODE) #X), Envoy::Stats::Gauge::ImportMode::MODE),
#define FINISH_STAT_DECL_UNIT_(X, UNIT) #X), Envoy::Stats::Histogram::Unit::UNIT),

#define FINISH_STAT_NAME_DECL_(X) X##_)),
#define FINISH_STAT_NAME_DECL_MODE_(X, MODE) X##_), Envoy::Stats::Gauge::ImportMode::MODE),
#define FINISH_STAT_NAME_DECL_UNIT_(X, UNIT) X##_), Envoy::Stats::Histogram::Unit::UNIT),


//#define WITH_STAT_CONTEXT(POOL, STAT_NAMES, PREFIX, EXPR) , EXPR
//#define STAT_NAME_DECL_(NAME)                                         \
//  , NAME##_(Envoy::Stats::Utility::counterFromStatNames((POOL), Envoy::statNameJoin(PREFIX, STAT_NAMES.NAME##_)))

// Used for declaring StatNames in a structure.
#define GENERATE_STAT_NAME_STRUCT(NAME, ...) Envoy::Stats::StatName NAME##_;
#define GENERATE_STAT_NAME_INIT(NAME, ...) , NAME##_(pool_.add(#NAME))
/*#define GENERATE_COUNTER_FROM_STAT_NAME(NAME) , NAME##_(scope.counterFromStatName(stat_names.NAME##_))
#define GENERATE_COUNTER_PREFIX_FROM_STAT_NAME(POOL, STAT_NAMES, PREFIX) \
  , Envoy::Stats::Utility::counterFromStatNames((POOL), Envoy::statNameJoin(PREFIX, STAT_NAMES.FINISH_STAT_NAME_DECL_
#define GENERATE_GAUGE_FROM_STAT_NAME(NAME, MODE) \
  , NAME##_(scope.gaugeFromStatName(stat_names.NAME##_, Envoy::Stats::Gauge::ImportMode::MODE))
#define GENERATE_GAUGE_PREFIX_FROM_STAT_NAME(POOL, STAT_NAMES, PREFIX)   \
  , Envoy::Stats::Utility::gaugeFromStatNames((POOL), Envoy::statNameJoin(PREFIX, STAT_NAMES.FINISH_STAT_NAME_DECL_MODE_
*/

static inline std::string statPrefixJoin(absl::string_view prefix, absl::string_view token) {
  if (prefix.empty()) {
    return std::string(token);
  } else if (absl::EndsWith(prefix, ".")) {
    // TODO(jmarantz): eliminate this case -- remove all the trailing dots from prefixes.
    return absl::StrCat(prefix, token);
  }
  return absl::StrCat(prefix, ".", token);
}

// Macros for declaring stat-structures using StatNames, for those that must be
// instantiated during operation, and where speed and scale matters.
#define MAKE_STATS_STRUCT_COUNTER_HELPER_(NAME) , NAME##_(Envoy::Stats::Utility::counterFromStatNames( \
    scope, {prefix, stat_names.NAME##_}))
#define MAKE_STATS_STRUCT_GAUGE_HELPER_(NAME, MODE) , NAME##_(Envoy::Stats::Utility::gaugeFromStatNames( \
    scope, {prefix, stat_names.NAME##_}, Envoy::Stats::Gauge::ImportMode::MODE))

#define MAKE_STAT_NAMES_STRUCT(StatNamesStruct, ALL_STATS) \
  struct StatNamesStruct { \
    explicit StatNamesStruct(Envoy::Stats::SymbolTable& symbol_table) \
      : pool_(symbol_table) \
        ALL_STATS(GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT) {} \
    Envoy::Stats::StatNamePool pool_;                                   \
    ALL_STATS(GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT)     \
  }

#define MAKE_STATS_STRUCT_STATNAME_HELPER_(name)
#define GENERATE_STATNAME_STRUCT(name)

#define MAKE_STATS_STRUCT(StatsStruct, StatNamesStruct, ALL_STATS)    \
  struct StatsStruct { \
    StatsStruct(const StatNamesStruct& stat_names, Envoy::Stats::StatName prefix, \
                Envoy::Stats::Scope& scope)                             \
        : scope_(scope) \
          ALL_STATS(MAKE_STATS_STRUCT_COUNTER_HELPER_, MAKE_STATS_STRUCT_GAUGE_HELPER_, \
                    MAKE_STATS_STRUCT_STATNAME_HELPER_) {}                     \
  Envoy::Stats::Scope& scope_; \
  ALL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_STATNAME_STRUCT)    \
}

#define POOL_COUNTER_PREFIX(POOL, PREFIX) (POOL).counterFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_
#define POOL_GAUGE_PREFIX(POOL, PREFIX) (POOL).gaugeFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_MODE_
#define POOL_HISTOGRAM_PREFIX(POOL, PREFIX) (POOL).histogramFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_UNIT_
#define POOL_TEXT_READOUT_PREFIX(POOL, PREFIX) (POOL).textReadoutFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_

#define POOL_COUNTER(POOL) POOL_COUNTER_PREFIX(POOL, "")
#define POOL_GAUGE(POOL) POOL_GAUGE_PREFIX(POOL, "")
#define POOL_HISTOGRAM(POOL) POOL_HISTOGRAM_PREFIX(POOL, "")
#define POOL_TEXT_READOUT(POOL) POOL_TEXT_READOUT_PREFIX(POOL, "")

#define NULL_STAT_DECL_(X) std::string(#X)),
#define NULL_STAT_DECL_IGNORE_MODE_(X, MODE) std::string(#X)),

#define NULL_POOL_GAUGE(POOL) (POOL).nullGauge(NULL_STAT_DECL_IGNORE_MODE_
} // namespace Envoy
