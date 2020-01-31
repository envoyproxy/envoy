#include "common/stats/stat_merger.h"

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Stats::Store& target_store) : store_(target_store) {}

StatMerger::~StatMerger() {
  applyCounters();
  applyGauges();
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas) {
  // Combine the incoming deltas with any leftover from the previous call to mergeCounters.
  for (const auto& counter : counter_deltas) {
    counter_deltas_[counter.first] += counter.second;
  }
  applyCounters();
}

void StatMerger::applyCounters() {
  if (counter_deltas_.empty()) {
    return;
  }

  absl::flat_hash_map<std::string, CounterSharedPtr> name_to_counter_map;
  for (const CounterSharedPtr& counter : store_.counters()) {
    name_to_counter_map[counter->name()] = counter;
  }

  // Iterate over the combined deltas, applying them to the active counters in
  // name_to_counter_map. If there is no match, e.g. because the child process
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
  // Combine the incoming values with any leftover from the previous call to mergeGauges.
  // Note that new values override old values, rather than adding to them.
  for (const auto& gauge : gauges) {
    gauge_values_[gauge.first] = gauge.second;
  }

  applyGauges();
}

void StatMerger::applyGauges() {
  if (gauge_values_.empty()) {
    return;
  }

  absl::flat_hash_map<std::string, GaugeSharedPtr> name_to_gauge_map;
  for (const GaugeSharedPtr& gauge : store_.gauges()) {
    name_to_gauge_map[gauge->name()] = gauge;
  }

  absl::flat_hash_map<std::string, uint64_t> unmatched;
  for (const auto& gauge : gauge_values_) {
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
    //    now we will leave the gauge in 'unmatched', which later swaps into
    //    gauge_values_, for application in the next call, or on destruction.
    //    import_mode==Uninitialized, and accumulate the parent value in
    //    gauge_ref.add(). Gauges in this mode will not be included in
    //    stats-sinks or the admin /stats calls, until the child initializes
    //    the gauge, in which case:
    // 3a. Child later initializes gauges as NeverImport: the parent value is
    //     cleared during the mergeImportMode call.
    // 3b. Child later initializes gauges as Accumulate: the parent value is
    //     retained.

    auto iter = name_to_gauge_map.find(gauge.first);
    if (iter == name_to_gauge_map.end()) {
      unmatched[gauge.first] = gauge.second;
      continue;
    }

    GaugeSharedPtr gauge_ptr = iter->second;
    Gauge& gauge_ref = *gauge_ptr;
    if (gauge_ref.importMode() == Gauge::ImportMode::NeverImport) {
      continue;
    }

    uint64_t& parent_value_ref = parent_gauge_values_[gauge.first];
    uint64_t old_parent_value = parent_value_ref;
    uint64_t new_parent_value = gauge.second;
    parent_value_ref = new_parent_value;

    // Note that new_parent_value may be less than old_parent_value, in which
    // case 2s complement does its magic (-1 == 0xffffffffffffffff) and adding
    // that to the gauge's current value works the same as subtraction.
    gauge_ref.add(new_parent_value - old_parent_value);
  }
  gauge_values_.swap(unmatched);
}

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                            const Protobuf::Map<std::string, uint64_t>& gauges) {
  mergeCounters(counter_deltas);
  mergeGauges(gauges);
}

} // namespace Stats
} // namespace Envoy
