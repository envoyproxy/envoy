#include "contrib/cryptomb/private_key_providers/source/cryptomb_stats.h"

#include "source/common/stats/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

CryptoMbStats::CryptoMbStats(Stats::Scope& scope, uint32_t max_queue_size,
                             absl::string_view stats_prefix,
                             absl::string_view queue_size_stat_prefix)
    : stat_name_pool_(scope.symbolTable()) {
  const Stats::StatName stats_prefix_statname(stat_name_pool_.add(stats_prefix));
  queue_size_counters_.reserve(max_queue_size);
  for (uint32_t i = 1; i <= max_queue_size; i++) {
    queue_size_counters_.push_back(Stats::Utility::counterFromStatNames(
        scope, {stats_prefix_statname, stat_name_pool_.add(absl::StrCat(queue_size_stat_prefix, i))}));
  }
}

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
