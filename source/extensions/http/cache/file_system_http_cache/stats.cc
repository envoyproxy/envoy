#include "source/extensions/http/cache/file_system_http_cache/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheStats generateStats(Stats::Scope& scope, absl::string_view cache_path) {
  const std::string full_prefix = absl::StrCat("cache_path=", cache_path, ".cache.");
  return {ALL_CACHE_STATS(POOL_COUNTER_PREFIX(scope, full_prefix),
                          POOL_GAUGE_PREFIX(scope, full_prefix))};
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
