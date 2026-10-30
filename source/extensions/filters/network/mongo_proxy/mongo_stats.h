#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

/**
 * All mongo proxy stats. @see stats_macros.h
 */
#define ALL_MONGO_PROXY_STATS(COUNTER, GAUGE, HISTOGRAM)                                           \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(cx_drain_close)                                                                          \
  COUNTER(decoding_error)                                                                          \
  COUNTER(delays_injected)                                                                         \
  COUNTER(op_command)                                                                              \
  COUNTER(op_command_reply)                                                                        \
  COUNTER(op_get_more)                                                                             \
  COUNTER(op_insert)                                                                               \
  COUNTER(op_kill_cursors)                                                                         \
  COUNTER(op_query)                                                                                \
  COUNTER(op_query_await_data)                                                                     \
  COUNTER(op_query_exhaust)                                                                        \
  COUNTER(op_query_multi_get)                                                                      \
  COUNTER(op_query_no_cursor_timeout)                                                              \
  COUNTER(op_query_no_max_time)                                                                    \
  COUNTER(op_query_scatter_get)                                                                    \
  COUNTER(op_query_tailable_cursor)                                                                \
  COUNTER(op_reply)                                                                                \
  COUNTER(op_reply_cursor_not_found)                                                               \
  COUNTER(op_reply_query_failure)                                                                  \
  COUNTER(op_reply_valid_cursor)                                                                   \
  GAUGE(op_query_active, Accumulate)

/**
 * Struct definition for all mongo proxy stats. @see stats_macros.h
 */
struct MongoProxyStats {
  ALL_MONGO_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class MongoStats {
public:
  MongoStats(Stats::Scope& scope, absl::string_view prefix,
             const std::vector<std::string>& commands);

  void incCounter(Stats::StatNameSpan base_names, Stats::StatNameTagSpan tags,
                  Stats::StatNameSpan names);
  void recordHistogram(Stats::StatNameSpan base_names, Stats::StatNameTagSpan tags,
                       Stats::StatNameSpan names, Stats::Histogram::Unit unit, uint64_t sample);

  /**
   * Finds or creates a StatName by string, taking a global lock if needed.
   *
   * TODO(jmarantz): Potential perf issue here with mutex contention for names
   * that have not been remembered as builtins in the constructor.
   */
  Stats::StatName getBuiltin(const std::string& str, Stats::StatName fallback) {
    return stat_name_set_->getBuiltin(str, fallback);
  }

  Stats::SymbolTable& symbolTable() { return scope_->symbolTable(); }

private:
  // mongo.(<stat_prefix>.)*: the scope carries the stat prefix as the tag of every stat.
  Stats::ScopeSharedPtr scope_;
  Stats::StatNameSetPtr stat_name_set_;

public:
  const Stats::StatName callsite_;
  const Stats::StatName cmd_;
  const Stats::StatName collection_;
  const Stats::StatName multi_get_;
  const Stats::StatName reply_num_docs_;
  const Stats::StatName reply_size_;
  const Stats::StatName reply_time_ms_;
  const Stats::StatName time_ms_;
  const Stats::StatName query_;
  const Stats::StatName scatter_get_;
  const Stats::StatName total_;
  const Stats::StatName unknown_command_;
  // The tag names of the per-callsite, per-command and per-collection tags.
  const Stats::StatName callsite_tag_;
  const Stats::StatName cmd_tag_;
  const Stats::StatName collection_tag_;
  // Created once at configuration load and shared by every filter instance, so that creating a
  // filter per connection does not re-resolve all the stats in the scope.
  MongoProxyStats proxy_stats_;
};
using MongoStatsSharedPtr = std::shared_ptr<MongoStats>;

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
