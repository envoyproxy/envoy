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
#include "envoy/stats/tag.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Abstract interface for allocating statistics. Implementations can
 * be created utilizing a single fixed-size block suitable for
 * shared-memory, or in the heap, allowing for pointers and sharing of
 * substrings, with an opportunity for reduced memory consumption.
 */
class StatDataAllocator {
public:
  virtual ~StatDataAllocator() {}

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the extracted tag values.
   * @return CounterSharedPtr a counter, or nullptr if allocation failed, in which case
   *     tag_extracted_name and tags are not moved.
   */
  virtual CounterSharedPtr makeCounter(absl::string_view name, std::string&& tag_extracted_name,
                                       std::vector<Tag>&& tags) PURE;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the extracted tag values.
   * @return GaugeSharedPtr a gauge, or nullptr if allocation failed, in which case
   *     tag_extracted_name and tags are not moved.
   */
  virtual GaugeSharedPtr makeGauge(absl::string_view name, std::string&& tag_extracted_name,
                                   std::vector<Tag>&& tags) PURE;

  /**
   * Determines whether this stats allocator requires bounded stat-name size.
   */
  virtual bool requiresBoundedStatNameSize() const PURE;

  // TODO(jmarantz): create a parallel mechanism to instantiate histograms. At
  // the moment, histograms don't fit the same pattern of counters and gaugaes
  // as they are not actually created in the context of a stats allocator.
};

} // namespace Stats
} // namespace Envoy
