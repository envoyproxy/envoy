#include "common/stats/stat_merger.h"

#include <regex>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : target_store_(target_store) {}

Gauge::CombineLogic StatMerger::getCombineLogic(Gauge& gauge, const std::string& gauge_name) {
  absl::optional<Gauge::CombineLogic> cached_logic = gauge.cachedCombineLogic();
  if (cached_logic.has_value()) {
    return cached_logic.value();
  }

  // Gauge name *substrings*, and special logic to use for combining those gauges' values.
  static const auto* nonstandard_combine_logic = new std::vector<
      std::pair<std::regex, Gauge::CombineLogic>>{
      // Any .version is either a static property of the binary, or an opaque identifier for
      // resources that are not passed across hot restart.
      {std::regex(".*\\.version$"), Gauge::CombineLogic::NoImport},
      // Once the child is up and reporting stats, its own control plane state and liveness is what
      // we're interested in.
      {std::regex(".*\\.control_plane.connected_state$"), Gauge::CombineLogic::NoImport},
      {std::regex("^server.live$"), Gauge::CombineLogic::NoImport},
      // Properties that should reasonably have some continuity across hot restart. The parent's
      // last value should be a relatively accurate starting point, and then the child can update
      // from there when appropriate. (All of these exceptional stats used with set() rather than
      // add()/sub(), so the child's new value will in fact overwrite.)
      {std::regex("^cluster_manager.active_clusters$"), Gauge::CombineLogic::NoImport},
      {std::regex("^cluster_manager.warming_clusters$"), Gauge::CombineLogic::NoImport},
      {std::regex("^cluster\\..*\\.membership_.*$"), Gauge::CombineLogic::NoImport},
      {std::regex("^cluster\\..*\\.max_host_weight$"), Gauge::CombineLogic::NoImport},
      {std::regex(".*\\.total_principals$"), Gauge::CombineLogic::NoImport},
      {std::regex("^listener_manager.total_listeners_active$"), Gauge::CombineLogic::NoImport},
      {std::regex("^overload\\..*\\.pressure$"), Gauge::CombineLogic::NoImport},
      // Due to the fd passing, the parent's view of whether its listeners are in transitive states
      // is not useful.
      {std::regex("^listener_manager.total_listeners_draining$"), Gauge::CombineLogic::NoImport},
      {std::regex("^listener_manager.total_listeners_warming$"), Gauge::CombineLogic::NoImport},
      // Static properties known at startup.
      {std::regex("^server.concurrency$"), Gauge::CombineLogic::NoImport},
      {std::regex("^server.hot_restart_epoch$"), Gauge::CombineLogic::NoImport},
      {std::regex("^runtime.admin_overrides_active$"), Gauge::CombineLogic::NoImport},
      {std::regex("^runtime.num_keys$"), Gauge::CombineLogic::NoImport},
  };
  for (const auto& exception : *nonstandard_combine_logic) {
    std::smatch match;
    if (std::regex_match(gauge_name, match, exception.first)) {
      gauge.setCombineLogic(exception.second);
      return exception.second;
    }
  }
  gauge.setCombineLogic(Gauge::CombineLogic::Accumulate);
  return Gauge::CombineLogic::Accumulate;
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas) {
  for (const auto& counter : counter_deltas) {
    target_store_.counter(counter.first).add(counter.second);
  }
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& gauge : gauges) {
    uint64_t& parent_value_ref = parent_gauge_values_[gauge.first.c_str()];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    auto& gauge_ref = target_store_.gauge(gauge.first);
    Gauge::CombineLogic combine_logic = StatMerger::getCombineLogic(gauge_ref, gauge.first);
    if (combine_logic == Gauge::CombineLogic::NoImport) {
      continue;
    }
    // If undefined, take parent's value unless explicitly told NoImport.
    if (!gauge_ref.used()) {
      gauge_ref.set(new_parent_value);
      continue;
    }
    switch (combine_logic) {
    case Gauge::CombineLogic::NoImport:
      break;
    case Gauge::CombineLogic::Accumulate:
      if (new_parent_value > old_parent_value) {
        gauge_ref.add(new_parent_value - old_parent_value);
      } else {
        gauge_ref.sub(old_parent_value - new_parent_value);
      }
      break;
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
