#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/symbol_table.h"
#include "envoy/stats/tag.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class Counter;
class Gauge;
class Histogram;
class NullGaugeImpl;
class Scope;
class TextReadout;

using CounterOptConstRef = absl::optional<std::reference_wrapper<const Counter>>;
using GaugeOptConstRef = absl::optional<std::reference_wrapper<const Gauge>>;
using HistogramOptConstRef = absl::optional<std::reference_wrapper<const Histogram>>;
using TextReadoutOptConstRef = absl::optional<std::reference_wrapper<const TextReadout>>;
using ScopePtr = std::unique_ptr<Scope>;
using ScopeSharedPtr = std::shared_ptr<Scope>;

template <class StatType> using IterateFn = std::function<bool(const RefcountPtr<StatType>&)>;

/**
 * A named scope for stats. Scopes are a grouping of stats that can be acted on as a unit if needed
 * (for example to free/delete all of them).
 */
class Scope {
public:
  virtual ~Scope() = default;

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * See also scopeFromStatName, which is preferred.
   *
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr createScope(const std::string& name) PURE;

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr scopeFromStatName(StatName name) PURE;

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) PURE;

  /**
   * Creates a Counter from the stat name. Tag extraction will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @return a counter within the scope's namespace.
   */
  Counter& counterFromStatName(const StatName& name) {
    return counterFromStatNameWithTags(name, absl::nullopt);
  }
  /**
   * Creates a Counter from the stat name and tags. If tags are not provided, tag extraction
   * will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param tags optionally specified tags.
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counterFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use counterFromStatName.
   * @param name The name, expressed as a string.
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counterFromString(const std::string& name) PURE;

  /**
   * Creates a Gauge from the stat name. Tag extraction will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param import_mode Whether hot-restart should accumulate this value.
   * @return a gauge within the scope's namespace.
   */
  Gauge& gaugeFromStatName(const StatName& name, Gauge::ImportMode import_mode) {
    return gaugeFromStatNameWithTags(name, absl::nullopt, import_mode);
  }

  /**
   * Creates a Gauge from the stat name and tags. If tags are not provided, tag extraction
   * will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param tags optionally specified tags.
   * @param import_mode Whether hot-restart should accumulate this value.
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Gauge::ImportMode import_mode) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use gaugeFromStatName.
   * @param name The name, expressed as a string.
   * @param import_mode Whether hot-restart should accumulate this value.
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) PURE;

  /**
   * @return a null gauge within the scope's namespace.
   */
  virtual NullGaugeImpl& nullGauge(const std::string& name) PURE;

  /**
   * Creates a Histogram from the stat name. Tag extraction will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param unit The unit of measurement.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  Histogram& histogramFromStatName(const StatName& name, Histogram::Unit unit) {
    return histogramFromStatNameWithTags(name, absl::nullopt, unit);
  }

  /**
   * Creates a Histogram from the stat name and tags. If tags are not provided, tag extraction
   * will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param tags optionally specified tags.
   * @param unit The unit of measurement.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogramFromStatNameWithTags(const StatName& name,
                                                   StatNameTagVectorOptConstRef tags,
                                                   Histogram::Unit unit) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use histogramFromStatName.
   * @param name The name, expressed as a string.
   * @param unit The unit of measurement.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) PURE;

  /**
   * Creates a TextReadout from the stat name. Tag extraction will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @return a text readout within the scope's namespace.
   */
  TextReadout& textReadoutFromStatName(const StatName& name) {
    return textReadoutFromStatNameWithTags(name, absl::nullopt);
  }

  /**
   * Creates a TextReadout from the stat name and tags. If tags are not provided, tag extraction
   * will be performed on the name.
   * @param name The name of the stat, obtained from the SymbolTable.
   * @param tags optionally specified tags.
   * @return a text readout within the scope's namespace.
   */
  virtual TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                                       StatNameTagVectorOptConstRef tags) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use textReadoutFromStatName.
   * @param name The name, expressed as a string.
   * @return a text readout within the scope's namespace.
   */
  virtual TextReadout& textReadoutFromString(const std::string& name) PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a counter within the scope's namespace, if it exists.
   */
  virtual CounterOptConstRef findCounter(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a gauge within the scope's namespace, if it exists.
   */
  virtual GaugeOptConstRef findGauge(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a histogram within the scope's namespace, if it
   * exists.
   */
  virtual HistogramOptConstRef findHistogram(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a text readout within the scope's namespace, if it exists.
   */
  virtual TextReadoutOptConstRef findTextReadout(StatName name) const PURE;

  /**
   * @return a reference to the symbol table.
   */
  virtual const SymbolTable& constSymbolTable() const PURE;
  virtual SymbolTable& symbolTable() PURE;

  /**
   * Calls 'fn' for every counter. Note that in the case of overlapping scopes,
   * the implementation may call fn more than one time for each counter. Iteration
   * stops if `fn` returns false;
   *
   * @param fn Function to be run for every counter, or until fn return false.
   * @return false if fn(counter) return false during iteration, true if every counter was hit.
   */
  virtual bool iterate(const IterateFn<Counter>& fn) const PURE;

  /**
   * Calls 'fn' for every gauge. Note that in the case of overlapping scopes,
   * the implementation may call fn more than one time for each gauge. Iteration
   * stops if `fn` returns false;
   *
   * @param fn Function to be run for every gauge, or until fn return false.
   * @return false if fn(gauge) return false during iteration, true if every gauge was hit.
   */
  virtual bool iterate(const IterateFn<Gauge>& fn) const PURE;

  /**
   * Calls 'fn' for every histogram. Note that in the case of overlapping
   * scopes, the implementation may call fn more than one time for each
   * histogram. Iteration stops if `fn` returns false;
   *
   * @param fn Function to be run for every histogram, or until fn return false.
   * @return false if fn(histogram) return false during iteration, true if every histogram was hit.
   */
  virtual bool iterate(const IterateFn<Histogram>& fn) const PURE;

  /**
   * Calls 'fn' for every text readout. Note that in the case of overlapping
   * scopes, the implementation may call fn more than one time for each
   * text readout. Iteration stops if `fn` returns false;
   *
   * @param fn Function to be run for every text readout, or until fn return false.
   * @return false if fn(text_readout) return false during iteration, true if every text readout
   *         was hit.
   */
  virtual bool iterate(const IterateFn<TextReadout>& fn) const PURE;
};

} // namespace Stats
} // namespace Envoy
