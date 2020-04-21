#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/symbol_table.h"
#include "envoy/stats/tag.h"

#include "absl/types/optional.h"
#include "absl/types/variant.h"

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

class DynamicName : public absl::string_view {
 public:
  DynamicName(absl::string_view s) : absl::string_view(s) {}
};

using Element = absl::variant<StatName, DynamicName>;
using ElementVec = std::vector<Element>;

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
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr createScope(const std::string& name) PURE;

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
   * Creates a counter from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param tags optionally specified tags.
   * @return A counter named using the joined elements.
   */
  Counter& counterFromElements(const ElementVec& elements,
                               StatNameTagVectorOptConstRef tags = absl::nullopt) {
    return counterFromElementsHelper(elements, tags);
  }
  virtual Counter& counterFromElementsHelper(const ElementVec& elements,
                                             StatNameTagVectorOptConstRef tags) PURE;


  /**
   * Creates a gauge from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param import_mode Whether hot-restart should accumulate this value.
   * @param tags optionally specified tags.
   * @return A gauge named using the joined elements.
   */
  Gauge& gaugeFromElements(const ElementVec& elements, Gauge::ImportMode import_mode,
                           StatNameTagVectorOptConstRef tags = absl::nullopt) {
    return gaugeFromElementsHelper(elements, import_mode, tags);
  }
  virtual Gauge& gaugeFromElementsHelper(const ElementVec& elements, Gauge::ImportMode import_mode,
                                         StatNameTagVectorOptConstRef tags) PURE;

  /**
   * Creates a histogram from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A histogram named using the joined elements.
   */
  Histogram& histogramFromElements(const ElementVec& elements, Histogram::Unit unit,
                                   StatNameTagVectorOptConstRef tags) {
    return histogramFromElementsHelper(elements, unit, tags);
  }
  virtual Histogram& histogramFromElementsHelper(const ElementVec& elements, Histogram::Unit unit,
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
};

} // namespace Stats
} // namespace Envoy
