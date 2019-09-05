#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/common/to_lower_table.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/network/common/redis/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

class RedisCommandStats {
public:
  RedisCommandStats(Stats::Scope& scope, const std::string& prefix, bool enabled);

  Stats::Counter& counter(const Stats::StatNameVec& stat_names);
  Stats::Histogram& histogram(const Stats::StatNameVec& stat_names);
  Stats::CompletableTimespanPtr createCommandTimer(Stats::StatName stat_name,
                                                   Envoy::TimeSource& time_source);
  Stats::CompletableTimespanPtr createAggregateTimer(Envoy::TimeSource& time_source);
  Stats::StatName getCommandFromRequest(const RespValue& request);
  void updateStatsTotal(Stats::StatName stat_name);
  void updateStats(const bool success, Stats::StatName stat_name);
  bool enabled() { return enabled_; }
  Stats::StatName getUnusedStatName() { return unused_metric_; }

private:
  void addCommandToPool(const std::string& command);

  Stats::Scope& scope_;
  Stats::StatNamePool stat_name_pool_;
  StringMap<Stats::StatName> stat_name_map_;
  const Stats::StatName prefix_;
  bool enabled_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName latency_;
  const Stats::StatName total_;
  const Stats::StatName success_;
  const Stats::StatName error_;
  const Stats::StatName unused_metric_;
  const Stats::StatName null_metric_;
  const Stats::StatName unknown_metric_;
  const ToLowerTable to_lower_table_;
};
using RedisCommandStatsPtr = std::shared_ptr<RedisCommandStats>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
