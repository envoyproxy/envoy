#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/tag.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class Counter;
class Gauge;
class Histogram;
class NullGaugeImpl;
class Scope;
class Store;
class TextReadout;

using CounterOptConstRef = absl::optional<std::reference_wrapper<const Counter>>;
using GaugeOptConstRef = absl::optional<std::reference_wrapper<const Gauge>>;
using HistogramOptConstRef = absl::optional<std::reference_wrapper<const Histogram>>;
using TextReadoutOptConstRef = absl::optional<std::reference_wrapper<const TextReadout>>;
using ConstScopeSharedPtr = std::shared_ptr<const Scope>;
using ScopeSharedPtr = std::shared_ptr<Scope>;

template <class StatType> using IterateFn = std::function<bool(const RefcountPtr<StatType>&)>;

/**
 * A named scope for stats. Scopes are a grouping of stats that can be acted on
 * as a unit if needed (for example to free/delete all of them).
 *
 * Every counters, gauges, histograms, and text-readouts is managed by a Scope.
 *
 * Scopes are managed by shared pointers. This makes it possible for the admin
 * stats handler to safely capture all the scope references and remain robust to
 * other threads deleting those scopes while rendering an admin stats page.
 *
 * It is invalid to allocate a Scope using std::unique_ptr or directly on the
 * stack.
 *
 * We use std::shared_ptr rather than Stats::RefcountPtr, which we use for other
 * stats, because:
 *  * existing uses of shared_ptr<Scope> exist in the Wasm extension and would
 *    need to be rewritten to allow for RefcountPtr<Scope>.
 *  * the main advantage of RefcountPtr is it's smaller per instance by 16
 *    bytes, but there are not typically enough scopes that the extra per-scope
 *    overhead would matter.
 *  * It's a little less coding to use enable_shared_from_this compared to
 *    adding a ref_count to the scope object, for each of its implementations.
 */
class Scope : public std::enable_shared_from_this<Scope> {
public:
  virtual ~Scope() = default;

  /** @return a shared_ptr for this */
  ScopeSharedPtr getShared() { return shared_from_this(); }

  /** @return a const shared_ptr for this */
  ConstScopeSharedPtr getConstShared() const { return shared_from_this(); }

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * See also scopeFromStatName, which is preferred.
   *
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopeSharedPtr createScope(const std::string& name) PURE;

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopeSharedPtr scopeFromStatName(StatName name) PURE;

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
   * Calls 'fn' for every counter. Iteration stops if `fn` returns false;
   *
   * @param fn Function to be run for every counter, or until fn return false.
   * @return false if fn(counter) return false during iteration, true if every counter was hit.
   */
  virtual bool iterate(const IterateFn<Counter>& fn) const PURE;

  /**
   * Calls 'fn' for every gauge. Iteration stops if `fn` returns false;
   *
   * @param fn Function to be run for every gauge, or until fn return false.
   * @return false if fn(gauge) return false during iteration, true if every gauge was hit.
   */
  virtual bool iterate(const IterateFn<Gauge>& fn) const PURE;

  /**
   * Calls 'fn' for every histogram. Iteration stops if `fn` returns false;
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

  /**
   * @return the aggregated prefix for this scope. A trailing dot is not
   * included, even if one was supplied when creating the scope. If this is a
   * nested scope, it will include names from every level. E.g.
   *     store.createScope("foo").createScope("bar").prefix() will be the StatName "foo.bar"
   */
  virtual StatName prefix() const PURE;

  /**
   * @return a reference to the Store object that owns this scope.
   */
  virtual Store& store() PURE;
  virtual const Store& constStore() const PURE;
};

} // namespace Stats
} // namespace Envoy
