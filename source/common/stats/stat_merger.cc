#include "common/stats/stat_merger.h"

#include <regex>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : target_store_(target_store) {}

absl::optional<StatMerger::CombineLogic>
StatMerger::getCombineLogic(const std::string& gauge_name) {
  // Gauge name *substrings*, and special logic to use for combining those gauges' values.
  static const std::vector<std::pair<std::regex, CombineLogic>> nonstandard_combine_logic{
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
      {std::regex("^cluster_manager.active_clusters$"), CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^cluster_manager.warming_clusters$"), CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^cluster\\..*\\.membership_total$"), CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^cluster\\..*\\.membership_healthy$"),
       CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^cluster\\..*\\.membership_degraded$"),
       CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^cluster\\..*\\.max_host_weight$"), CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex(".*\\.total_principals$"), CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^listener_manager.total_listeners_active$"),
       CombineLogic::OnlyImportWhenUnusedInChild},
      {std::regex("^overload\\..*\\.pressure$"), CombineLogic::OnlyImportWhenUnusedInChild},
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
  for (const auto& exception : nonstandard_combine_logic) {
    std::smatch match;
    if (std::regex_match(gauge_name, match, exception.first)) {
      return exception.second;
    }
  }
  return absl::nullopt;
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counters) {
  for (const auto& counter : counters) {
    uint64_t new_parent_value = counter.second;
    const auto& found_value = parent_counter_values_.find(counter.first.c_str());
    if (found_value == parent_counter_values_.end()) {
      target_store_.counter(counter.first).stealthyAdd(new_parent_value);
    } else {
      uint64_t old_parent_value = found_value->second;
      target_store_.counter(counter.first).add(new_parent_value - old_parent_value);
    }
    parent_counter_values_[counter.first.c_str()] = new_parent_value;
  }
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& gauge : gauges) {
    uint64_t& parent_value_ref = parent_gauge_values_[gauge.first.c_str()];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    CombineLogic combine_logic =
        StatMerger::getCombineLogic(gauge.first).value_or(CombineLogic::Accumulate);
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
    case CombineLogic::OnlyImportWhenUnusedInChild:
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

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counters,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  mergeCounters(counters);
  mergeGauges(gauges);
}

} // namespace Stats
} // namespace Envoy
