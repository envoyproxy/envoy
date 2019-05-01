#include "common/stats/stat_merger.h"

#include <regex>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : target_store_(target_store) {}

bool StatMerger::shouldImport(Gauge& gauge, const std::string& gauge_name) {
  absl::optional<bool> should_import = gauge.cachedShouldImport();
  if (should_import.has_value()) {
    return should_import.value();
  }

  // Gauge name *substrings*, and special logic to use for combining those gauges' values.
  static const auto* nonstandard_combine_logic = new std::vector<std::regex>{
      // Any .version is either a static property of the binary, or an opaque identifier for
      // resources that are not passed across hot restart.
      std::regex(".*\\.version$"),
      // Once the child is up and reporting stats, its own control plane state and liveness is what
      // we're interested in.
      std::regex(".*\\.control_plane.connected_state$"),
      std::regex("^server.live$"),
      // Properties that should reasonably have some continuity across hot restart. The parent's
      // last value should be a relatively accurate starting point, and then the child can update
      // from there when appropriate. (All of these exceptional stats used with set() rather than
      // add()/sub(), so the child's new value will in fact overwrite.)
      std::regex("^cluster_manager.active_clusters$"),
      std::regex("^cluster_manager.warming_clusters$"),
      std::regex("^cluster\\..*\\.membership_.*$"),
      std::regex("^cluster\\..*\\.max_host_weight$"),
      std::regex(".*\\.total_principals$"),
      std::regex("^listener_manager.total_listeners_active$"),
      std::regex("^overload\\..*\\.pressure$"),
      // Due to the fd passing, the parent's view of whether its listeners are in transitive states
      // is not useful.
      std::regex("^listener_manager.total_listeners_draining$"),
      std::regex("^listener_manager.total_listeners_warming$"),
      // Static properties known at startup.
      std::regex("^server.concurrency$"),
      std::regex("^server.hot_restart_epoch$"),
      std::regex("^runtime.admin_overrides_active$"),
      std::regex("^runtime.num_keys$"),
  };
  for (const auto& exception : *nonstandard_combine_logic) {
    std::smatch match;
    if (std::regex_match(gauge_name, match, exception)) {
      gauge.setShouldImport(false);
      return false;
    }
  }
  gauge.setShouldImport(true);
  return true;
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas) {
  for (const auto& counter : counter_deltas) {
    target_store_.counter(counter.first).add(counter.second);
  }
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& gauge : gauges) {
    auto& gauge_ref = target_store_.gauge(gauge.first);
    uint64_t& parent_value_ref = parent_gauge_values_[gauge_ref.statName()];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    if (!StatMerger::shouldImport(gauge_ref, gauge.first)) {
      continue;
    }
    if (new_parent_value > old_parent_value) {
      gauge_ref.add(new_parent_value - old_parent_value);
    } else {
      gauge_ref.sub(old_parent_value - new_parent_value);
    }
  }
}

// TODO(fredlas) the current implementation can "leak" obsolete parent stats into the child.
// That is, the parent had stat "foo", the child doesn't care about "foo" and back in the
// shared memory implementation would have dropped it, but the import causes it to be made into
// a real stat that stays around forever. The initial mini-consensus approach will be to
// track which stats are actually getting used by the child, and drop those that aren't when
// the hot restart completes.
void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  mergeCounters(counter_deltas);
  mergeGauges(gauges);
}

} // namespace Stats
} // namespace Envoy
