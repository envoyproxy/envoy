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
  RedisCommandStats(Stats::SymbolTable& symbol_table, const std::string& prefix);

  // TODO (@FAYiEKcbD0XFqF2QK2E4viAHg8rMm2VbjYKdjTg): Use Singleton to manage a single
  // RedisCommandStats on the client factory so that it can be used for proxy filter, discovery and
  // health check.
  static std::shared_ptr<RedisCommandStats>
  createRedisCommandStats(Stats::SymbolTable& symbol_table) {
    return std::make_shared<Common::Redis::RedisCommandStats>(symbol_table, "upstream_commands");
  }

  Stats::Counter& counter(Stats::Scope& scope, const Stats::StatNameVec& stat_names);
  Stats::Histogram& histogram(Stats::Scope& scope, const Stats::StatNameVec& stat_names,
                              Stats::Histogram::Unit unit);
  Stats::TimespanPtr createCommandTimer(Stats::Scope& scope, Stats::StatName command,
                                        Envoy::TimeSource& time_source);
  Stats::TimespanPtr createAggregateTimer(Stats::Scope& scope, Envoy::TimeSource& time_source);
  Stats::StatName getCommandFromRequest(const RespValue& request);
  void updateStatsTotal(Stats::Scope& scope, Stats::StatName command);
  void updateStats(Stats::Scope& scope, Stats::StatName command, const bool success);
  Stats::StatName getUnusedStatName() { return unused_metric_; }

private:
  Stats::SymbolTable& symbol_table_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName prefix_;
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
using RedisCommandStatsSharedPtr = std::shared_ptr<RedisCommandStats>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
