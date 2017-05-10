#pragma once

#include <string>

#include "envoy/stats/stats.h"

namespace Lyft {
/**
 * These are helper macros for allocating "fixed" stats throughout the code base in a way that
 * is also easy to mock and test. The general flow looks like this:
 *
 * Define a block of stats like this:
 *   #define MY_COOL_STATS(COUNTER, GAUGE, TIMER) \
 *     COUNTER(counter1)
 *     GAUGE(gauge1)
 *     TIMER(timer1)
 *     ...
 *
 * Now actually put these stats somewhere, usually as a member of a struct:
 *   struct MyCoolStats {
 *     MY_COOL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
 *   };
 *
 * Finally, when you want to actually instantiate the above struct using a Stats::Pool, you do:
 *   MyCoolStats stats{
 *     MY_COOL_STATS(POOL_COUNTER(...), POOL_GAUGE(...), POOL_TIMER(...))};
 */

#define GENERATE_COUNTER_STRUCT(NAME) Stats::Counter& NAME##_;
#define GENERATE_GAUGE_STRUCT(NAME) Stats::Gauge& NAME##_;
#define GENERATE_TIMER_STRUCT(NAME) Stats::Timer& NAME##_;

#define FINISH_STAT_DECL_(X) + std::string(#X)),

#define POOL_COUNTER_PREFIX(POOL, PREFIX) (POOL).counter(PREFIX FINISH_STAT_DECL_
#define POOL_GAUGE_PREFIX(POOL, PREFIX) (POOL).gauge(PREFIX FINISH_STAT_DECL_
#define POOL_TIMER_PREFIX(POOL, PREFIX) (POOL).timer(PREFIX FINISH_STAT_DECL_

#define POOL_COUNTER(POOL) POOL_COUNTER_PREFIX(POOL, "")
#define POOL_GAUGE(POOL) POOL_GAUGE_PREFIX(POOL, "")
#define POOL_TIMER(POOL) POOL_TIMER_PREFIX(POOL, "")
} // Lyft