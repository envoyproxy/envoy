#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"
#include "envoy/stats/tag.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Abstract interface for allocating statistics. Implementations can
 * be created utilizing a single fixed-size block suitable for
 * shared-memory, or in the heap, allowing for pointers and sharing of
 * substrings, with an opportunity for reduced memory consumption.
 * TODO(fredlas) this interface can be deleted now that the shared memory version is gone.
 */
class StatDataAllocator {
public:
  virtual ~StatDataAllocator() = default;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the extracted tag values.
   * @return CounterSharedPtr a counter, or nullptr if allocation failed, in which case
   *     tag_extracted_name and tags are not moved.
   */
  virtual CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                                       const std::vector<Tag>& tags) PURE;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the extracted tag values.
   * @return GaugeSharedPtr a gauge, or nullptr if allocation failed, in which case
   *     tag_extracted_name and tags are not moved.
   */
  virtual GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                                   const std::vector<Tag>& tags,
                                   Gauge::ImportMode import_mode) PURE;

  virtual const SymbolTable& constSymbolTable() const PURE;
  virtual SymbolTable& symbolTable() PURE;

  // TODO(jmarantz): create a parallel mechanism to instantiate histograms. At
  // the moment, histograms don't fit the same pattern of counters and gauges
  // as they are not actually created in the context of a stats allocator.
};

} // namespace Stats
} // namespace Envoy
