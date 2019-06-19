#include "common/stats/stat_merger.h"

#include <regex>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : temp_scope_(target_store.createScope("")) {}

bool StatMerger::shouldImportBasedOnRegex(const std::string& gauge_name) {
  // Gauge name *substrings*, and special logic to use for combining those gauges' values.
  static const auto* nonstandard_combine_logic = new std::vector<std::regex>{
      // Any .version is either a static property of the binary, or an opaque identifier for
      // resources that are not passed across hot restart.
      std::regex(".*\\.version$"),
      std::regex("^version$"),
      // Once the child is up and reporting stats, its own control plane state and liveness is what
      // we're interested in.
      std::regex(".*\\.control_plane.connected_state$"),
      std::regex("^control_plane.connected_state$"),
      std::regex("^server.live$"),
      // Properties that should reasonably have some continuity across hot restart. The parent's
      // last value should be a relatively accurate starting point, and then the child can update
      // from there when appropriate. (All of these exceptional stats used with set() rather than
      // add()/sub(), so the child's new value will in fact overwrite.)
      std::regex("^cluster_manager.active_clusters$"),
      std::regex("^cluster_manager.warming_clusters$"),
      std::regex("^cluster\\..*\\.membership_.*$"),
      std::regex("^membership_.*$"),
      std::regex("^cluster\\..*\\.max_host_weight$"),
      std::regex("^max_host_weight$"),
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
      std::regex("^runtime.num_layers$"),
  };
  std::smatch match;
  for (const auto& exception : *nonstandard_combine_logic) {
    if (std::regex_match(gauge_name, match, exception)) {
      return false;
    }
  }
  return true;
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas) {
  for (const auto& counter : counter_deltas) {
    temp_scope_->counter(counter.first).add(counter.second);
  }
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& gauge : gauges) {
    StatNameManagedStorage storage(gauge.first, temp_scope_->symbolTable());
    StatName stat_name = storage.statName();
    absl::optional<std::reference_wrapper<const Gauge>> gauge_opt =
        temp_scope_->findGauge(stat_name);

    // If the stat named in the protobuf map is already initialized, and has a
    // mode of NeverImport, then we simply skip over the map entry. This is a
    // case where a new revision of Envoy has been built where a previously
    // Accumulated gauge has been switched to NeverImport mode.
    Gauge::ImportMode import_mode = Gauge::ImportMode::Uninitialized;
    if (gauge_opt) {
      import_mode = gauge_opt->get().importMode();
      if (import_mode == Gauge::ImportMode::NeverImport) {
        continue;
      }
    }

    // We establish here a tentative import-mode of Uninitialized. Gauges in
    // this mode will not be reported in stats sinks or in the admin /stats
    // endpoint. However, we'll retain the transferred value, and if the running
    // system initialized the stat as Accumulate, we'll have the accumulated
    // value ready to go. If the system re-initializes it as NeverImport, we'll
    // clear the value during the mergeImportMode call.
    auto& gauge_ref = temp_scope_->gaugeFromStatName(stat_name, import_mode);
    uint64_t& parent_value_ref = parent_gauge_values_[gauge_ref.statName()];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    if (new_parent_value > old_parent_value) {
      gauge_ref.add(new_parent_value - old_parent_value);
    } else {
      gauge_ref.sub(old_parent_value - new_parent_value);
    }
  }
}

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  mergeCounters(counter_deltas);
  mergeGauges(gauges);
}

} // namespace Stats
} // namespace Envoy
