#pragma once

#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "source/common/stats/symbol_table_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

using StatsCounterRef = std::reference_wrapper<Stats::Counter>;

class CryptoMbStats {
public:
  CryptoMbStats(Stats::Scope& scope, uint32_t max_queue_size, absl::string_view stats_prefix,
                absl::string_view queue_size_stat_prefix);
  std::vector<StatsCounterRef>& getQueueSizeCounters() { return queue_size_counters_; }

private:
  Stats::StatNamePool stat_name_pool_;
  const Stats::StatName stats_prefix_;
  // Vector for queue size statistics.
  std::vector<StatsCounterRef> queue_size_counters_;
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy