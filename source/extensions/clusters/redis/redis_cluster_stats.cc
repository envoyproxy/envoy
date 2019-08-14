#include "redis_cluster_stats.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

RedisClusterStats:RedisClusterStats(Stats::Scope& scope, const std::string& prefix) 
    : scope_(scope), prefix_(stat_name_set_.add(prefix + "redis")) {}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
