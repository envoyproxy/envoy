#include "source/common/stats/stat_merger.h"

#include <algorithm>

namespace Envoy {
namespace Stats {

StatMerger::StatMerger(Store& target_store) : temp_scope_(target_store.createScope("")) {}

StatMerger::~StatMerger() {
  // By the time a parent exits, all its contributions to accumulated gauges
  // should be zero. But depending on the timing of the stat-merger
  // communication shutdown and other shutdown activities on the parent, the
  // gauges may not all be zero yet. So simply erase all the parent
  // contributions.
  for (StatName stat_name : parent_gauges_) {
    Gauge& gauge = temp_scope_->gaugeFromStatName(stat_name, Gauge::ImportMode::Uninitialized);
    gauge.setParentValue(0);
  }
}

StatName StatMerger::DynamicContext::makeDynamicStatName(const std::string& name,
                                                         const DynamicsMap& map) {
  auto iter = map.find(name);
  if (iter == map.end()) {
    return symbolic_pool_.add(name);
  }

  const DynamicSpans& dynamic_spans = iter->second;
  auto dynamic = dynamic_spans.begin();
  auto dynamic_end = dynamic_spans.end();

  // Name has embedded dynamic segments; we'll need to join together the
  // static/dynamic StatName segments.
  StatNameVec segments;
  uint32_t segment_index = 0;
  std::vector<absl::string_view> dynamic_segments;

  for (auto segment : absl::StrSplit(name, '.')) {
    if (dynamic != dynamic_end && dynamic->first == segment_index) {
      // Handle start of dynamic span. We note that we are in a dynamic
      // span by adding to dynamic_segments, which should of course be
      // non-empty.
      ASSERT(dynamic_segments.empty());
      if (dynamic->second == segment_index) {
        // Handle start==end (a one-segment span).
        segments.push_back(dynamic_pool_.add(segment));
        ++dynamic;
      } else {
        // Handle start<end, so we save the first segment in dynamic_segments.
        dynamic_segments.push_back(segment);
      }
    } else if (dynamic_segments.empty()) {
      // Handle that we are not in dynamic mode; we are just allocating
      // a symbolic segment.
      segments.push_back(symbolic_pool_.add(segment));
    } else {
      // Handle the next dynamic segment.
      dynamic_segments.push_back(segment);
      if (dynamic->second == segment_index) {
        // Handle that this dynamic segment is the last one, and we're flipping
        // back to symbolic mode.
        segments.push_back(dynamic_pool_.add(absl::StrJoin(dynamic_segments, ".")));
        dynamic_segments.clear();
        ++dynamic;
      }
    }
    ++segment_index;
  }
  ASSERT(dynamic_segments.empty());
  ASSERT(dynamic == dynamic_end);

  storage_ptr_ = symbol_table_.join(segments);
  return StatName(storage_ptr_.get());
}

void StatMerger::mergeCounters(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                               const DynamicsMap& dynamic_map) {
  for (const auto& counter : counter_deltas) {
    const std::string& name = counter.first;
    StatMerger::DynamicContext dynamic_context(temp_scope_->symbolTable());
    StatName stat_name = dynamic_context.makeDynamicStatName(name, dynamic_map);
    temp_scope_->counterFromStatName(stat_name).add(counter.second);
  }
}

void StatMerger::mergeGauges(const Protobuf::Map<std::string, uint64_t>& gauges,
                             const DynamicsMap& dynamic_map) {
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
    //    gauge_ref.add(). Gauges in this mode will be included in
    //    stats-sinks or the admin /stats calls, until the child initializes
    //    the gauge, in which case:
    // 3a. Child later initializes gauges as NeverImport: the parent value is
    //     cleared during the mergeImportMode call.
    // 3b. Child later initializes gauges as Accumulate: the parent value is
    //     retained.

    StatMerger::DynamicContext dynamic_context(temp_scope_->symbolTable());
    StatName stat_name = dynamic_context.makeDynamicStatName(gauge.first, dynamic_map);
    GaugeOptConstRef gauge_opt = temp_scope_->findGauge(stat_name);

    Gauge::ImportMode import_mode = Gauge::ImportMode::Uninitialized;
    if (gauge_opt) {
      import_mode = gauge_opt->get().importMode();
      if (import_mode == Gauge::ImportMode::NeverImport) {
        continue;
      }
    }

    // TODO(snowp): Propagate tag values during hot restarts.
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

    const uint64_t new_parent_value = gauge.second;
    parent_gauges_.insert(gauge_ref.statName());
    gauge_ref.setParentValue(new_parent_value);
  }
}

void StatMerger::retainParentGaugeValue(Stats::StatName gauge_name) {
  parent_gauges_.erase(gauge_name);
}

void StatMerger::mergeStats(const Protobuf::Map<std::string, uint64_t>& counter_deltas,
                            const Protobuf::Map<std::string, uint64_t>& gauges,
                            const DynamicsMap& dynamics) {
  mergeCounters(counter_deltas, dynamics);
  mergeGauges(gauges, dynamics);
}

} // namespace Stats
} // namespace Envoy
