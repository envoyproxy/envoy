#pragma once

#include <unordered_map>

#include "envoy/stats/store.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Stats {

// Responsible for the sensible merging of two instances of the same type of stat from two different
// (typically hot restart parent+child) Envoy processes.
class StatMerger {
public:
  StatMerger(Stats::Store& target_store);

  // Merge the values of stats_proto into stats_store. Counters are always straightforward
  // addition, while gauges default to addition but have exceptions.
  void mergeStats(const Protobuf::Map<std::string, uint64_t>& counters,
                  const Protobuf::Map<std::string, uint64_t>& gauges);

private:
  enum class CombineLogic {
    Accumulate, // the default
    OnlyImportWhenUnused,
    NoImport,
    BooleanOr,
  };
  std::unordered_map<std::string, uint64_t> parent_counter_values_;
  std::unordered_map<std::string, uint64_t> parent_gauge_values_;
  Stats::Store& target_store_;
};

} // namespace Stats
} // namespace Envoy
