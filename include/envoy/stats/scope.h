#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/symbol_table.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class Counter;
class Gauge;
class Histogram;
class Scope;
class NullGaugeImpl;

typedef std::unique_ptr<Scope> ScopePtr;
typedef std::shared_ptr<Scope> ScopeSharedPtr;

/**
 * A named scope for stats. Scopes are a grouping of stats that can be acted on as a unit if needed
 * (for example to free/delete all of them).
 */
class Scope {
public:
  virtual ~Scope() {}

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
   * @param name The name of the stat, obtained from the SymbolTable.
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counterFromStatName(StatName name) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use counterFromStatName.
   * @param name The name, expressed as a string.
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counter(const std::string& name) PURE;

  /**
   * @param name The name of the stat, obtained from the SymbolTable.
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gaugeFromStatName(StatName name) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use gaugeFromStatName.
   * @param name The name, expressed as a string.
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gauge(const std::string& name) PURE;

  /**
   * @return a null gauge within the scope's namespace.
   */
  virtual NullGaugeImpl& nullGauge(const std::string& name) PURE;

  /**
   * @param name The name of the stat, obtained from the SymbolTable.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogramFromStatName(StatName name) PURE;

  /**
   * TODO(#6667): this variant is deprecated: use histogramFromStatName.
   * @param name The name, expressed as a string.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogram(const std::string& name) PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a counter within the scope's namespace, if it exists.
   */
  virtual absl::optional<std::reference_wrapper<const Counter>>
  findCounter(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a gauge within the scope's namespace, if it exists.
   */
  virtual absl::optional<std::reference_wrapper<const Gauge>> findGauge(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a histogram within the scope's namespace, if it
   * exists.
   */
  virtual absl::optional<std::reference_wrapper<const Histogram>>
  findHistogram(StatName name) const PURE;

  /**
   * @return a reference to the symbol table.
   */
  virtual const SymbolTable& constSymbolTable() const PURE;
  virtual SymbolTable& symbolTable() PURE;

  /**
   * Performs fast conversion of string to StatName in the hot-path, at the
   * expense of significant per-name-per-thread memory overhead. This incurs
   * a lock only the first time this string is referenced from a thread. It
   * should only be used names that cannot be determined during startup or xDS
   * update. Forthose names that can be determined at that time, the StatNames
   * should be collected via StatNamePool or StatNameManagedStorage once, and
   * then used later on in the hot-path to compose fully elaborated StatName
   * objects via SymbolTable::join(), which is lock-free.
   *
   * @param name The name of the stat or fragment of stat.
   * @return the stat name allocated from symbolTable().
   */
  virtual StatName fastMemoryIntensiveStatNameLookup(absl::string_view name) PURE;
};

} // namespace Stats
} // namespace Envoy
