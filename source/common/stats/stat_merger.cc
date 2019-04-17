#include "common/stats/stat_merger.h"

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
  static const std::vector<std::pair<std::string, CombineLogic>> combine_logic_exceptions{
      {".version", CombineLogic::NoImport},
      {".connected_state", CombineLogic::NoImport},
      {"server.live", CombineLogic::NoImport},
      {"runtime.admin_overrides_active", CombineLogic::NoImport},
      {"runtime.num_keys", CombineLogic::NoImport},
      {"cluster_manager.active_clusters", CombineLogic::OnlyImportWhenUnused},
      {"cluster_manager.warming_clusters", CombineLogic::OnlyImportWhenUnused},
      {".membership_total", CombineLogic::OnlyImportWhenUnused},
      {".membership_healthy", CombineLogic::OnlyImportWhenUnused},
      {".membership_degraded", CombineLogic::OnlyImportWhenUnused},
      {".max_host_weight", CombineLogic::OnlyImportWhenUnused},
      {".total_principals", CombineLogic::OnlyImportWhenUnused},
      {"listener_manager.total_listeners_draining", CombineLogic::NoImport},
      {"listener_manager.total_listeners_warming", CombineLogic::OnlyImportWhenUnused},
      {"listener_manager.total_listeners_active", CombineLogic::OnlyImportWhenUnused},
      {"pressure", CombineLogic::OnlyImportWhenUnused},
      {"server.concurrency", CombineLogic::OnlyImportWhenUnused},
      {"server.hot_restart_epoch", CombineLogic::NoImport},
  };
  for (const auto& gauge : gauges) {
    uint64_t new_parent_value = gauge.second;
    auto found_value = parent_gauge_values_.find(gauge.first);
    uint64_t old_parent_value = found_value == parent_gauge_values_.end() ? 0 : found_value->second;
    parent_gauge_values_[gauge.first] = new_parent_value;
    CombineLogic combine_logic = CombineLogic::Accumulate;
    for (auto exception : combine_logic_exceptions) {
      if (gauge.first.find(exception.first) != std::string::npos) {
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
