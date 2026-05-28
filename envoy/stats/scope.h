#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/tag.h"

#include "source/common/stats/symbol_table.h"

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

// Settings for limiting the number of counters, gauges and histograms allowed
// in a scope. This currently only supports thread local stats.
struct ScopeStatsLimitSettings {
  // Max number of counters allowed in this scope. absl::nullopt means no limit.
  absl::optional<uint32_t> max_counters = absl::nullopt;
  // Max number of gauges allowed in this scope. absl::nullopt means no limit.
  absl::optional<uint32_t> max_gauges = absl::nullopt;
  // Max number of histograms allowed in this scope. absl::nullopt means no limit.
  absl::optional<uint32_t> max_histograms = absl::nullopt;
};

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
   * Set a callback to be run when the scope is destroyed.
   * @param callback the callback to run.
   */
  virtual void setCleanupCallback(std::function<void()> callback) = 0;

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * See also scopeFromStatName, which is preferred.
   *
   * @param name supplies the scope's namespace prefix.
   * @param evictable whether unused metrics can be deleted from the scope caches. This requires
   * that the metrics are not stored by reference.
   * @param limits metric limits for counters, gauges and histograms allowed in this scope.
   * @param matcher optional per-scope stats matcher; replaces the store-level matcher when set.
   * NOTE: If the scope specific matcher is set, then the sub scope will inherit the same matcher
   * unless another matcher is explicitly set.
   */
  ScopeSharedPtr createScope(const std::string& name, bool evictable = false,
                             const ScopeStatsLimitSettings& limits = {},
                             StatsMatcherSharedPtr matcher = nullptr) {
    return createScope(name, {}, absl::string_view{}, evictable, limits, std::move(matcher));
  }

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   *
   * @param name supplies the scope's namespace prefix.
   * @param evictable whether unused metrics can be deleted from the scope caches. This requires
   * that the metrics are not stored by reference.
   * @param limits metric limits for counters, gauges and histograms allowed in this scope.
   * @param matcher optional per-scope stats matcher; replaces the store-level matcher when set.
   * NOTE: If the scope specific matcher is set, then the sub scope will inherit the same matcher
   * unless another matcher is explicitly set.
   */
  ScopeSharedPtr scopeFromStatName(StatName name, bool evictable = false,
                                   const ScopeStatsLimitSettings& limits = {},
                                   StatsMatcherSharedPtr matcher = nullptr) {
    return scopeFromStatName(name, {}, StatName(), evictable, limits, std::move(matcher));
  }

  /**
   * Allocate a new scope, optionally supplying tags and a pre-built tagged name (with tag values
   * interleaved). NOTE: The behavior is implementation-defined for now because both legacy and
   * tag-aware scopes are still supported.
   *
   * @param name supplies the scope's tag-extracted namespace prefix (no tag values).
   * @param name_tags tags to associate with (and propagate from) the scope, as string_view pairs.
   * @param tagged_name optional explicit flat namespace prefix with tag values interleaved.
   * NOTE:
   * - If `name_tags` are empty, the `tagged_name` will be ignored and `name` will be used as both
   *   the tag-extracted and the flat namespace prefix.
   * - If `name_tags` are non-empty and `tagged_name` is empty, the flat tagged name will be derived
   *   by joining `name` and `name_tags`.
   * - If `name_tags` are non-empty and `tagged_name` is non-empty, the `tagged_name` will be used
   *   as provided, and is expected to already have the tag values joined in the correct places.
   *   The caller is responsible for ensuring the `tagged_name` matches the `name` and `name_tags`.
   *
   * @param evictable whether unused metrics can be deleted from the scope caches.
   * @param limits metric limits for counters, gauges and histograms allowed in this scope.
   * @param matcher optional per-scope stats matcher; replaces the store-level matcher when set.
   */
  virtual ScopeSharedPtr createScope(absl::string_view name, StringViewTagSpan name_tags,
                                     absl::string_view tagged_name, bool evictable = false,
                                     const ScopeStatsLimitSettings& limits = {},
                                     StatsMatcherSharedPtr matcher = nullptr) PURE;

  /**
   * Allocate a new scope from a StatName, optionally supplying tags and a pre-built tagged name
   * (with tag values interleaved). See the `createScope` variant for details and notes.
   *
   * @param name supplies the scope's tag-extracted namespace prefix (no tag values).
   * @param name_tags tags to associate with (and propagate from) the scope.
   * @param tagged_name optional explicit flat namespace prefix with tag values interleaved.
   * @param evictable whether unused metrics can be deleted from the scope caches.
   * @param limits metric limits for counters, gauges and histograms allowed in this scope.
   * @param matcher optional per-scope stats matcher; replaces the store-level matcher when set.
   */
  virtual ScopeSharedPtr scopeFromStatName(StatName name, StatNameTagSpan name_tags,
                                           StatName tagged_name, bool evictable = false,
                                           const ScopeStatsLimitSettings& limits = {},
                                           StatsMatcherSharedPtr matcher = nullptr) PURE;

  /**
   * Creates a Counter from the tag-extracted name, tags and an optional pre-built tagged name.
   * @param name The tag-extracted name of the stat (no tag values), obtained from the SymbolTable.
   * @param name_tags optionally specified tags.
   * @param tagged_name optional pre-built flat name (with tag values interleaved) relative to the
   * scope.
   * NOTE:
   * - If `name_tags` are empty, the `tagged_name` will be ignored and `name` will be used as both
   *   the tag-extracted name and the flat name.
   * - If `name_tags` are non-empty and `tagged_name` is empty, the flat name will be derived by
   *   joining `name` and `name_tags`.
   * - If `name_tags` are non-empty and `tagged_name` is non-empty, the `tagged_name` will be used
   *   as provided, and is expected to already have the tag values joined in the correct places.
   *   The caller is responsible for ensuring the `tagged_name` matches the `name` and `name_tags`.
   *
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counterFromStatName(StatName name, absl::optional<StatNameTagSpan> name_tags,
                                       StatName tagged_name) PURE;

  /**
   * Creates a Gauge from the tag-extracted name, tags and an optional pre-built tagged name.
   * See the `counterFromStatName` variant for details and notes on name_tags and tagged_name.
   *
   * @param name The tag-extracted name of the stat (no tag values), obtained from the SymbolTable.
   * @param name_tags optionally specified tags.
   * @param tagged_name optional pre-built flat name (with tag values interleaved).
   * @param import_mode Whether hot-restart should accumulate this value.
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gaugeFromStatName(StatName name, absl::optional<StatNameTagSpan> name_tags,
                                   StatName tagged_name, Gauge::ImportMode import_mode) PURE;

  /**
   * Creates a Histogram from the tag-extracted name, tags and an optional pre-built tagged name.
   * See the `counterFromStatName` variant for details and notes on name_tags and tagged_name.
   *
   * @param name The tag-extracted name of the stat (no tag values), obtained from the SymbolTable.
   * @param name_tags optionally specified tags.
   * @param tagged_name optional pre-built flat name (with tag values interleaved).
   * @param unit The unit of measurement.
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogramFromStatName(StatName name, absl::optional<StatNameTagSpan> name_tags,
                                           StatName tagged_name, Histogram::Unit unit) PURE;

  /**
   * Creates a TextReadout from the tag-extracted name, tags and an optional pre-built tagged name.
   * See the `counterFromStatName` variant for details and notes on name_tags and tagged_name.
   *
   * @param name The tag-extracted name of the stat (no tag values), obtained from the SymbolTable.
   * @param name_tags optionally specified tags.
   * @param tagged_name optional pre-built flat name (with tag values interleaved).
   * @return a text readout within the scope's namespace.
   */
  virtual TextReadout& textReadoutFromStatName(StatName name,
                                               absl::optional<StatNameTagSpan> name_tags,
                                               StatName tagged_name) PURE;

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
  Counter& counterFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags) {
    return counterFromStatName(name, toTagSpan(tags), StatName());
  }

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
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) {
    return gaugeFromStatName(name, toTagSpan(tags), StatName(), import_mode);
  }

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
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) {
    return histogramFromStatName(name, toTagSpan(tags), StatName(), unit);
  }

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
  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) {
    return textReadoutFromStatName(name, toTagSpan(tags), StatName());
  }

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

private:
  // Converts the legacy optional-vector-reference tag representation accepted by the deprecated
  // *FromStatNameWithTags convenience methods into the span representation taken by the new
  // *FromStatName virtual methods. The returned span aliases the caller-owned vector and must only
  // be used for the duration of the (synchronous) call.
  static absl::optional<StatNameTagSpan> toTagSpan(StatNameTagVectorOptConstRef tags) {
    if (tags.has_value()) {
      return StatNameTagSpan(tags->get());
    }
    return absl::nullopt;
  }
};

} // namespace Stats
} // namespace Envoy
