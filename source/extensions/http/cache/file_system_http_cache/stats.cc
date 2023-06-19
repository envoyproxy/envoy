#include "source/extensions/http/cache/file_system_http_cache/stats.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheStats generateStats(CacheStatNames& stat_names, Stats::Scope& scope,
                         absl::string_view cache_path) {
  Stats::StatName cache_path_statname =
      stat_names.pool_.add(absl::StrReplaceAll(cache_path, {{".", "_"}}));
  return {stat_names, scope, cache_path_statname};
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
