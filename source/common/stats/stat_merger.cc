#include "common/stats/stat_merger.h"

#include <regex>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : target_store_(target_store) {}

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counters,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& counter : counters) {
    uint64_t new_parent_value = counter.second;
    auto found_value = parent_counter_values_.find(counter.first);
    uint64_t old_parent_value =
        found_value == parent_counter_values_.end() ? 0 : found_value->second;
    parent_counter_values_[counter.first] = new_parent_value;
    target_store_.counter(counter.first).add(new_parent_value - old_parent_value);
  }

  // Gauge name *substrings*, and special logic to use for combining those gauges' values.
  static const std::vector<std::pair<std::regex, CombineLogic>> combine_logic_exceptions{
      // Any .version is either a static property of the binary, or an opaque identifier for
      // resources that are not passed across hot restart.
      {std::regex(".*\\.version$"), CombineLogic::NoImport},
      // Once the child is up and reporting stats, its own control plane state and liveness is what
      // we're interested in.
      {std::regex(".*\\.control_plane.connected_state$"), CombineLogic::NoImport},
      {std::regex("^server.live$"), CombineLogic::NoImport},
      // Properties that should reasonably have some continuity across hot restart. The parent's
      // last value should be a relatively accurate starting point, and then the child can update
      // from there when appropriate. (All of these exceptional stats used with set() rather than
      // add()/sub(), so the child's new value will in fact overwrite.)
      {std::regex("^cluster_manager.active_clusters$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster_manager.warming_clusters$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_total$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_healthy$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_degraded$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.max_host_weight$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex(".*\\.total_principals$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^listener_manager.total_listeners_active$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^overload\\..*\\.pressure$"), CombineLogic::OnlyImportWhenUnused},
      // Due to the fd passing, the parent's view of whether its listeners are in transitive states
      // is not useful.
      {std::regex("^listener_manager.total_listeners_draining$"), CombineLogic::NoImport},
      {std::regex("^listener_manager.total_listeners_warming$"), CombineLogic::NoImport},
      // Static properties known at startup.
      {std::regex("^server.concurrency$"), CombineLogic::NoImport},
      {std::regex("^server.hot_restart_epoch$"), CombineLogic::NoImport},
      {std::regex("^runtime.admin_overrides_active$"), CombineLogic::NoImport},
      {std::regex("^runtime.num_keys$"), CombineLogic::NoImport},
  };
  for (const auto& gauge : gauges) {
    uint64_t new_parent_value = gauge.second;
    auto found_value = parent_gauge_values_.find(gauge.first);
    uint64_t old_parent_value = found_value == parent_gauge_values_.end() ? 0 : found_value->second;
    parent_gauge_values_[gauge.first] = new_parent_value;
    CombineLogic combine_logic = CombineLogic::Accumulate;
    for (auto exception : combine_logic_exceptions) {
      std::smatch match;
      if (std::regex_match(gauge.first, match, exception.first)) {
        combine_logic = exception.second;
        break;
      }
    }
    if (combine_logic == CombineLogic::NoImport) {
      continue;
    }
    auto& gauge_ref = target_store_.gauge(gauge.first);
    // If undefined, take parent's value unless explicitly told NoImport.
    if (!gauge_ref.used()) {
      gauge_ref.set(new_parent_value);
      continue;
    }
    switch (combine_logic) {
    case CombineLogic::OnlyImportWhenUnused:
      // Already set above; nothing left to do.
      break;
    case CombineLogic::NoImport:
      break;
    case CombineLogic::Accumulate:
      if (new_parent_value > old_parent_value) {
        gauge_ref.add(new_parent_value - old_parent_value);
      } else {
        gauge_ref.sub(old_parent_value - new_parent_value);
      }
      break;
    }
  }
}

} // namespace Stats
} // namespace Envoy
