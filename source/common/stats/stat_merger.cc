#include "common/stats/stat_merger.h"

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : temp_scope_(target_store.createScope("")),
                                                     store_(target_store) {}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas) {
  absl::flat_hash_map<std::string, CounterSharedPtr> name_to_counter_map;
  for (const CounterSharedPtr& counter : store_.counters()) {
    name_to_counter_map[counter->name()] = counter;
  }

  // Combine the incoming deltas with any leftover from the previous call to mergeCounters.
  for (const auto& counter : counter_deltas) {
    counter_deltas_[counter.first] += counter.second;
  }

  // Iterate over the combined deltas, applying them to the active counters in
  // name_to_counter_map.  If there is no match, e.g. because the child process
  // has not populated the stats being sent yet, then we save them for the next
  // iteration of mergeCounters in 'unmatched', which we'll swap into the data
  // structure afterward, so we don't modify a map we are iterating over.
  absl::flat_hash_map<std::string, uint64_t> unmatched;
  for (const auto& counter : counter_deltas_) {
    auto iter = name_to_counter_map.find(counter.first);
    if (iter == name_to_counter_map.end()) {
      unmatched[counter.first] = counter.second;
    } else {
      iter->second->add(counter.second);
    }
  }
  counter_deltas_.swap(unmatched);
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges) {
  for (const auto& gauge : gauges) {
    // Merging gauges via RPC from the parent has 3 cases; case 1 and 3b are the
    // most common.
    //
    // 1. Child thinks gauge is Accumulate : data is combined in
    //    gauge_ref.add() below.
    // 2. Child thinks gauge is NeverImport: we skip this loop entry via
    //    'continue'.
    // 3. Child has not yet initialized gauge yet -- this merge is the
    //    first time the child learns of the gauge. It's possible the child
    //    will think the gauge is NeverImport due to a code change. But for
    //    now we will leave the gauge in the child process as
    //    import_mode==Uninitialized, and accumulate the parent value in
    //    gauge_ref.add(). Gauges in this mode will not be included in
    //    stats-sinks or the admin /stats calls, until the child initializes
    //    the gauge, in which case:
    // 3a. Child later initializes gauges as NeverImport: the parent value is
    //     cleared during the mergeImportMode call.
    // 3b. Child later initializes gauges as Accumulate: the parent value is
    //     retained.

    StatNameManagedStorage storage(gauge.first, temp_scope_->symbolTable());
    StatName stat_name = storage.statName();
    GaugeOptConstRef gauge_opt = temp_scope_->findGauge(stat_name);

    Gauge::ImportMode import_mode = Gauge::ImportMode::Uninitialized;
    if (gauge_opt) {
      import_mode = gauge_opt->get().importMode();
      if (import_mode == Gauge::ImportMode::NeverImport) {
        continue;
      }
    }

    auto& gauge_ref = temp_scope_->gaugeFromStatName(stat_name, import_mode);
    if (gauge_ref.importMode() == Gauge::ImportMode::NeverImport) {
      // On the first iteration through the loop, the gauge will not be loaded into the scope
      // cache even though it might exist in another scope. Thus, we need to check again for
      // the import status to see if we should skip this gauge.
      //
      // TODO(mattklein123): There is a race condition here. It's technically possible that
      // between the time we created this stat, the stat might be created by the child as a
      // never import stat, making the below math invalid. A follow up solution is to take the
      // store lock starting from gaugeFromStatName() to the end of this function, but this will
      // require adding some type of mergeGauge() function to the scope and dealing with recursive
      // lock acquisition, etc. so we will leave this as a follow up. This race should be incredibly
      // rare.
      continue;
    }

    uint64_t& parent_value_ref = parent_gauge_values_[gauge_ref.statName()];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    // Note that new_parent_value may be less than old_parent_value, in which
    // case 2s complement does its magic (-1 == 0xffffffffffffffff) and adding
    // that to the gauge's current value works the same as subtraction.
    gauge_ref.add(new_parent_value - old_parent_value);
  }
}

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  mergeCounters(counter_deltas);
  mergeGauges(gauges);
}

} // namespace Stats
} // namespace Envoy
