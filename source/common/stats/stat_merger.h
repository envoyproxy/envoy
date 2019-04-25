#pragma once

#include "envoy/stats/store.h"

#include "common/common/hash.h"
#include "common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

// Responsible for the sensible merging of two instances of the same stat from two different
// (typically hot restart parent+child) Envoy processes.
class StatMerger {
public:
  StatMerger(Stats::Store& target_store);

  enum class CombineLogic {
    Accumulate,                  // the default; the merged result is old+new.
    OnlyImportWhenUnusedInChild, // import parent value only if child stat is undefined. (So, just
                                 // once.)
    NoImport, // ignore parent entirely; child stat is undefined until it sets its own value.
  };

  // Merge the values of stats_proto into stats_store. Counters are always straightforward
  // addition, while gauges default to addition but have exceptions.
  void mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_values,
                  const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                  const Protobuf::Map<std::string, uint64_t>& gauges);

  // TODO(fredlas) add void verifyCombineLogicSpecified(absl::string_view gauge_name), to
  // be called at gauge allocation, to ensure (with an ASSERT) that anyone adding a new stat
  // will be forced to come across this code and explicitly specify combination logic.

  // Looks up gauge_name in our nonstandard combine logic rules, and returns the result (nullopt
  // meaning not nonstandard, implying Accumulate).
  static absl::optional<StatMerger::CombineLogic> getCombineLogic(const std::string& gauge_name);

private:
  void mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_values,
                     const Protobuf::Map<std::string, uint64_t>& counter_deltas);
  void mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges);
  ConstCharStarHashMap<uint64_t> parent_gauge_values_;
  Stats::Store& target_store_;
};

} // namespace Stats
} // namespace Envoy
