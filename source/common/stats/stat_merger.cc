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
      {std::regex(".*\\.version$"), CombineLogic::NoImport},
      {std::regex(".*\\.control_plane.connected_state$"), CombineLogic::NoImport},
      {std::regex("^server.live$"), CombineLogic::NoImport},
      {std::regex("^runtime.admin_overrides_active$"), CombineLogic::NoImport},
      {std::regex("^runtime.num_keys$"), CombineLogic::NoImport},
      {std::regex("^cluster_manager.active_clusters$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster_manager.warming_clusters$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_total$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_healthy$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.membership_degraded$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^cluster\\..*\\.max_host_weight$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex(".*\\.total_principals$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^listener_manager.total_listeners_draining$"), CombineLogic::NoImport},
      {std::regex("^listener_manager.total_listeners_warming$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^listener_manager.total_listeners_active$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^overload\\..*\\.pressure$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^server.concurrency$"), CombineLogic::OnlyImportWhenUnused},
      {std::regex("^server.hot_restart_epoch$"), CombineLogic::NoImport},
  };
  for (const auto& gauge : gauges) {
    uint64_t new_parent_value = gauge.second;
    auto found_value = parent_gauge_values_.find(gauge.first);
    uint64_t old_parent_value = found_value == parent_gauge_values_.end() ? 0 : found_value->second;
    parent_gauge_values_[gauge.first] = new_parent_value;
    CombineLogic combine_logic = CombineLogic::Accumulate;
    for (auto exception : combine_logic_exceptions) {
      std::smatch match;
      if(std::regex_match(gauge.first, match, exception.first)) {
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
