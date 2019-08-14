#pragma once

#include "envoy/stats/scope.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class RedisClusterStats {
public:
  RedisClusterStats(Stats::Scope& scope, const std::string& prefix);

private:
  Stats::Scope& scope_;
  const Stats::StatName prefix_;

};
using RedisClusterStatsPtr = std::shared_ptr<RedisClusterStats>;

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
