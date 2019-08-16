#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

class RedisCommandStats {
public:
  RedisCommandStats(Stats::Scope& scope, const std::string& prefix, bool enableCommandCounts);

  Stats::Counter& counter(std::string name);
  Stats::Histogram&
  histogram(Stats::StatName statName); // This is only used by upstream_rq_time_ currently, so no
                                       // need to do a dynamic lookup
  // Could add histogram timer for each command in future

private:
  Stats::SymbolTable::StoragePtr addPrefix(const Stats::StatName name);

  Stats::Scope& scope_;
  Stats::StatNameSet stat_name_set_;
  const Stats::StatName prefix_;

public:
  const Stats::StatName upstream_rq_time_;
};
using RedisCommandStatsPtr = std::shared_ptr<RedisCommandStats>;

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
