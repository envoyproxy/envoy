#pragma once

#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

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
 * use clang-format-on/off etc. "bazel run //tools/code_format:check_format -- fix" will take
 * care of lining up the backslashes.
 *
 * Now actually put these stats somewhere, usually as a member of a struct:
 *   struct MyCoolStats {
 *     MY_COOL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
 *   };
 *
 * Finally, when you want to actually instantiate the above struct using a Stats::Pool, you do:
 *   MyCoolStats stats{
 *     MY_COOL_STATS(POOL_COUNTER(...), POOL_GAUGE(...), POOL_HISTOGRAM(...))};
 *
 *
 * The above constructs are the simplest way to declare counters, gauges,
 * histograms, and text-readouts in your data structures. However they incur
 * some overhead to symbolize the names every time they are instantiated. For
 * data structures that are re-instantiated extensively during operation,
 * e.g. in response to an xDS update, We can separately instantiate a
 * StatNameStruct, containing symbolized names for each stat. That should be
 * instantiated once at startup and held in some context or factory. Do that
 * with:
 *
 *    MAKE_STAT_NAMES_STRUCT(MyStatNames, MY_COOL_STATS);
 *
 * This generates a structure definition with a constructor that requires a
 * SymbolTable. So you must, in a context instantiated once, initialize with:
 *
 *    : my_cool_stat_names_(stat_store.symbolTable())
 *
 * Once you have the StatNamesStruct instance declared, you can create a stats
 * struct efficiently during operation (e.g. in an xDS handler) with
 *
 *    MAKE_STATS_STRUCT(MyStats, MyStatNames, MY_COOL_STATS);
 *
 * This new structure is constructed with 2 or 3 args:
 *    1. The stat_names struct created from MAKE_STAT_NAMES_STRUCT
 *    2. The scope in which to instantiate the stats
 *    3. An optional prefix, which will be prepended to each stat name.
 * For example:
 *
 *    : my_cool_stats_(context.my_cool_stat_names_, scope, opt_prefix)
 */

// Fully-qualified for use in external callsites.
#define GENERATE_COUNTER_STRUCT(NAME) Envoy::Stats::Counter& NAME##_;
#define GENERATE_GAUGE_STRUCT(NAME, MODE) Envoy::Stats::Gauge& NAME##_;
#define GENERATE_HISTOGRAM_STRUCT(NAME, UNIT) Envoy::Stats::Histogram& NAME##_;
#define GENERATE_TEXT_READOUT_STRUCT(NAME) Envoy::Stats::TextReadout& NAME##_;

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

#define POOL_COUNTER_PREFIX(POOL, PREFIX) (POOL).counterFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_
#define POOL_GAUGE_PREFIX(POOL, PREFIX) (POOL).gaugeFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_MODE_
#define POOL_HISTOGRAM_PREFIX(POOL, PREFIX) (POOL).histogramFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_UNIT_
#define POOL_TEXT_READOUT_PREFIX(POOL, PREFIX) (POOL).textReadoutFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_
#define POOL_STAT_NAME_PREFIX(POOL, PREFIX) (POOL).symbolTable().textReadoutFromString(Envoy::statPrefixJoin(PREFIX, FINISH_STAT_DECL_

#define POOL_COUNTER(POOL) POOL_COUNTER_PREFIX(POOL, "")
#define POOL_GAUGE(POOL) POOL_GAUGE_PREFIX(POOL, "")
#define POOL_HISTOGRAM(POOL) POOL_HISTOGRAM_PREFIX(POOL, "")
#define POOL_TEXT_READOUT(POOL) POOL_TEXT_READOUT_PREFIX(POOL, "")

#define NULL_STAT_DECL_(X) std::string(#X)),
#define NULL_STAT_DECL_IGNORE_MODE_(X, MODE) std::string(#X)),

#define NULL_POOL_GAUGE(POOL) (POOL).nullGauge(NULL_STAT_DECL_IGNORE_MODE_

// Used for declaring StatNames in a structure.
#define GENERATE_STAT_NAME_STRUCT(NAME, ...) Envoy::Stats::StatName NAME##_;
#define GENERATE_STAT_NAME_INIT(NAME, ...) , NAME##_(pool_.add(#NAME))

// Used for defining constrcutors of stat objects
#define GENERATE_CONSTRUCTOR_PARAM(NAME) Envoy::Stats::Counter &NAME,
#define GENERATE_CONSTRUCTOR_COUNTER_PARAM(NAME) Envoy::Stats::Counter &NAME,
#define GENERATE_CONSTRUCTOR_GAUGE_PARAM(NAME, ...) Envoy::Stats::Gauge &NAME,
#define GENERATE_CONSTRUCTOR_INIT_LIST(NAME, ...) , NAME##_(NAME)

// Macros for declaring stat-structures using StatNames, for those that must be
// instantiated during operation, and where speed and scale matters. These
// macros are not for direct use; they are only for use from
// MAKE_STAT_NAMES_STRUCT. and MAKE_STAT_STRUCT.
#define MAKE_STATS_STRUCT_COUNTER_HELPER_(NAME)                                                    \
  , NAME##_(Envoy::Stats::Utility::counterFromStatNames(scope, {prefix, stat_names.NAME##_}))
#define MAKE_STATS_STRUCT_GAUGE_HELPER_(NAME, MODE)                                                \
  , NAME##_(Envoy::Stats::Utility::gaugeFromStatNames(scope, {prefix, stat_names.NAME##_},         \
                                                      Envoy::Stats::Gauge::ImportMode::MODE))
#define MAKE_STATS_STRUCT_HISTOGRAM_HELPER_(NAME, UNIT)                                            \
  , NAME##_(Envoy::Stats::Utility::histogramFromStatNames(scope, {prefix, stat_names.NAME##_},     \
                                                          Envoy::Stats::Histogram::Unit::UNIT))
#define MAKE_STATS_STRUCT_TEXT_READOUT_HELPER_(NAME)                                               \
  , NAME##_(Envoy::Stats::Utility::textReadoutFromStatNames(scope, {prefix, stat_names.NAME##_}))

#define MAKE_STATS_STRUCT_STATNAME_HELPER_(name)
#define GENERATE_STATNAME_STRUCT(name)

/**
 * Generates a struct with StatNames for a subsystem, based on the stats macro
 * with COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, and STATNAME calls. The
 * ALL_STATS macro must have all 5 parameters.
 */
#define MAKE_STAT_NAMES_STRUCT(StatNamesStruct, ALL_STATS)                                         \
  struct StatNamesStruct {                                                                         \
    explicit StatNamesStruct(Envoy::Stats::SymbolTable& symbol_table)                              \
        : pool_(symbol_table)                                                                      \
              ALL_STATS(GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT, \
                        GENERATE_STAT_NAME_INIT, GENERATE_STAT_NAME_INIT) {}                       \
    Envoy::Stats::StatNamePool pool_;                                                              \
    ALL_STATS(GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT,     \
              GENERATE_STAT_NAME_STRUCT, GENERATE_STAT_NAME_STRUCT)                                \
  }

/**
 * Instantiates a structure of stats based on a new scope and optional prefix,
 * using a predefined structure of stat names. A reference to the stat_names is
 * also stored in the structure, for two reasons: (a) as a syntactic convenience
 * for using macros to generate the comma separators for the initializer and (b)
 * as a convenience at the call-site to access STATNAME-declared names from the
 * stats structure.
 */
#define MAKE_STATS_STRUCT(StatsStruct, StatNamesStruct, ALL_STATS)                                 \
  struct StatsStruct {                                                                             \
    /* Also referenced in Stats::createDeferredCompatibleStats. */                                 \
    using StatNameType = StatNamesStruct;                                                          \
    static const absl::string_view typeName() { return #StatsStruct; }                             \
    StatsStruct(const StatNamesStruct& stat_names, Envoy::Stats::Scope& scope,                     \
                Envoy::Stats::StatName prefix = Envoy::Stats::StatName())                          \
        : stat_names_(stat_names)                                                                  \
              ALL_STATS(MAKE_STATS_STRUCT_COUNTER_HELPER_, MAKE_STATS_STRUCT_GAUGE_HELPER_,        \
                        MAKE_STATS_STRUCT_HISTOGRAM_HELPER_,                                       \
                        MAKE_STATS_STRUCT_TEXT_READOUT_HELPER_,                                    \
                        MAKE_STATS_STRUCT_STATNAME_HELPER_) {}                                     \
    const StatNameType& stat_names_;                                                               \
    ALL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT,           \
              GENERATE_TEXT_READOUT_STRUCT, GENERATE_STATNAME_STRUCT)                              \
  }
} // namespace Envoy
