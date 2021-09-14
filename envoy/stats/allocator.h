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
 */
class Allocator {
public:
  virtual ~Allocator() = default;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the tag values.
   * @return CounterSharedPtr a counter.
   */
  virtual CounterSharedPtr makeCounter(StatName name, StatName tag_extracted_name,
                                       const StatNameTagVector& stat_name_tags) PURE;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param stat_name_tags the tag values.
   * @return GaugeSharedPtr a gauge.
   */
  virtual GaugeSharedPtr makeGauge(StatName name, StatName tag_extracted_name,
                                   const StatNameTagVector& stat_name_tags,
                                   Gauge::ImportMode import_mode) PURE;

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the tag values.
   * @return TextReadoutSharedPtr a text readout.
   */
  virtual TextReadoutSharedPtr makeTextReadout(StatName name, StatName tag_extracted_name,
                                               const StatNameTagVector& stat_name_tags) PURE;
  virtual const SymbolTable& constSymbolTable() const PURE;
  virtual SymbolTable& symbolTable() PURE;

  /**
   * Mark rejected stats as deleted by moving them to a different vector, so they don't show up
   * when iterating over stats, but prevent crashes when trying to access references to them.
   * Note that allocating a stat with the same name after calling this will
   * return a new stat. Hence callers should seek to avoid this situation, as is
   * done in ThreadLocalStore.
   */
  virtual void markCounterForDeletion(const CounterSharedPtr& counter) PURE;
  virtual void markGaugeForDeletion(const GaugeSharedPtr& gauge) PURE;
  virtual void markTextReadoutForDeletion(const TextReadoutSharedPtr& text_readout) PURE;

  /**
   * Iterate over all stats that need to be added to a sink. Note, that implementations can
   * potentially hold on to a mutex that will deadlock if the passed in functors try to create
   * or delete a stat.
   * @param f_size functor that is provided the number of all stats in the sink. Note this is
   *        called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat in the sink at a time.
   */
  virtual void forEachCounter(std::function<void(std::size_t)> f_size,
                              std::function<void(Stats::Counter&)> f_stat) const PURE;
  virtual void forEachGauge(std::function<void(std::size_t)> f_size,
                            std::function<void(Stats::Gauge&)> f_stat) const PURE;
  virtual void forEachTextReadout(std::function<void(std::size_t)> f_size,
                                  std::function<void(Stats::TextReadout&)> f_stat) const PURE;

  // TODO(jmarantz): create a parallel mechanism to instantiate histograms. At
  // the moment, histograms don't fit the same pattern of counters and gauges
  // as they are not actually created in the context of a stats allocator.
};

} // namespace Stats
} // namespace Envoy
