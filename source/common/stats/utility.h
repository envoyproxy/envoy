#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/common/thread.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

/**
 * Represents a dynamically created stat name token based on absl::string_view.
 * This class wrapper is used in the 'Element' variant so that call-sites
 * can express explicit intent to create dynamic stat names, which are more
 * expensive than symbolic stat names. We use dynamic stat names only for
 * building stats based on names discovered in the line of a request.
 */
class DynamicName : public absl::string_view {
public:
  // This is intentionally left as an implicit conversion from string_view to
  // make call-sites easier to read, e.g.
  //    Utility::counterFromElements(*scope, {DynamicName("a"), DynamicName("b")});
  DynamicName(absl::string_view str) : absl::string_view(str) {}
};

/**
 * Holds either a symbolic StatName or a dynamic string, for the purpose of
 * composing a vector to pass to Utility::counterFromElements, etc. This is
 * a programming convenience to create joined stat names. It is easier to
 * call the above helpers than to use SymbolTable::join(), because the helpers
 * hide the memory management of the joined storage, and they allow easier
 * co-mingling of symbolic and dynamic stat-name components.
 */
using Element = absl::variant<StatName, DynamicName>;
using ElementVec = absl::InlinedVector<Element, 8>;

/**
 * Common stats utility routines.
 */
class Utility {
public:
  /**
   * ':' is a reserved char in statsd. Do a character replacement to avoid
   * costly inline translations later.
   *
   * @param name the stat name to sanitize.
   * @return the sanitized stat name.
   */
  static std::string sanitizeStatsName(absl::string_view name);

  /**
   * Finds a metric tag with the specified name.
   *
   * @param metric The metric in which the tag is expected to exist.
   * @param find_tag_name The name of the tag to search for.
   * @return The value of the tag, if found.
   */
  static absl::optional<StatName> findTag(const Metric& metric, StatName find_tag_name);

  /**
   * Creates a counter from a vector of tokens which are used to create the
   * name. The tokens can be specified as DynamicName or StatName. For
   * tokens specified as DynamicName, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * See also counterFromStatNames, which is slightly faster but does not allow
   * passing DynamicName(string)s as names.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param tags optionally specified tags.
   * @return A counter named using the joined elements.
   */
  static Counter& counterFromElements(Scope& scope, const ElementVec& elements,
                                      StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a counter from a vector of tokens which are used to create the
   * name. The tokens must be of type StatName.
   *
   * See also counterFromElements, which is slightly slower, but allows
   * passing DynamicName(string)s as elements.
   *
   * @param scope The scope in which to create the counter.
   * @param names The vector of StatNames
   * @param tags optionally specified tags.
   * @return A counter named using the joined elements.
   */
  static Counter& counterFromStatNames(Scope& scope, const StatNameVec& names,
                                       StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a gauge from a vector of tokens which are used to create the
   * name. The tokens can be specified as DynamicName or StatName. For
   * tokens specified as DynamicName, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * See also gaugeFromStatNames, which is slightly faster but does not allow
   * passing DynamicName(string)s as names.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param import_mode Whether hot-restart should accumulate this value.
   * @param tags optionally specified tags.
   * @return A gauge named using the joined elements.
   */
  static Gauge& gaugeFromElements(Scope& scope, const ElementVec& elements,
                                  Gauge::ImportMode import_mode,
                                  StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a gauge from a vector of tokens which are used to create the
   * name. The tokens must be of type StatName.
   *
   * See also gaugeFromElements, which is slightly slower, but allows
   * passing DynamicName(string)s as elements.
   *
   * @param scope The scope in which to create the counter.
   * @param names The vector of StatNames
   * @param import_mode Whether hot-restart should accumulate this value.
   * @param tags optionally specified tags.
   * @return A gauge named using the joined elements.
   */
  static Gauge& gaugeFromStatNames(Scope& scope, const StatNameVec& elements,
                                   Gauge::ImportMode import_mode,
                                   StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a histogram from a vector of tokens which are used to create the
   * name. The tokens can be specified as DynamicName or StatName. For
   * tokens specified as DynamicName, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * See also histogramFromStatNames, which is slightly faster but does not allow
   * passing DynamicName(string)s as names.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A histogram named using the joined elements.
   */
  static Histogram& histogramFromElements(Scope& scope, const ElementVec& elements,
                                          Histogram::Unit unit,
                                          StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a histogram from a vector of tokens which are used to create the
   * name. The tokens must be of type StatName.
   *
   * See also histogramFromElements, which is slightly slower, but allows
   * passing DynamicName(string)s as elements.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A histogram named using the joined elements.
   */
  static Histogram& histogramFromStatNames(Scope& scope, const StatNameVec& elements,
                                           Histogram::Unit unit,
                                           StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a TextReadout from a vector of tokens which are used to create the
   * name. The tokens can be specified as DynamicName or StatName. For
   * tokens specified as DynamicName, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * See also TextReadoutFromStatNames, which is slightly faster but does not allow
   * passing DynamicName(string)s as names.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A TextReadout named using the joined elements.
   */
  static TextReadout& textReadoutFromElements(Scope& scope, const ElementVec& elements,
                                              StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a TextReadout from a vector of tokens which are used to create the
   * name. The tokens must be of type StatName.
   *
   * See also TextReadoutFromElements, which is slightly slower, but allows
   * passing DynamicName(string)s as elements.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed DynamicName and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A TextReadout named using the joined elements.
   */
  static TextReadout& textReadoutFromStatNames(Scope& scope, const StatNameVec& elements,
                                               StatNameTagVectorOptConstRef tags = absl::nullopt);
};

/**
 * Holds a reference to a stat by name. Note that the stat may not be created
 * yet at the time CachedReference is created. Calling get() then does a lazy
 * lookup, potentially returning absl::nullopt if the stat doesn't exist yet.
 * StatReference works whether the name was constructed symbolically, or with
 * StatNameDynamicStorage.
 *
 * Lookups are very slow, taking time proportional to the size of the scope,
 * holding mutexes during the lookup. However once the lookup succeeds, the
 * result is cached atomically, and further calls to get() are thus fast and
 * mutex-free. The implementation may be faster for stats that are named
 * symbolically.
 *
 * CachedReference is valid for the lifetime of the Scope. When the Scope
 * becomes invalid, CachedReferences must also be dropped as they will hold
 * pointers into the scope.
 */
template <class StatType> class CachedReference {
public:
  CachedReference(Scope& scope, absl::string_view name) : scope_(scope), name_(std::string(name)) {}

  /**
   * Finds the named stat, if it exists, returning it as an optional.
   */
  absl::optional<std::reference_wrapper<StatType>> get() {
    StatType* stat = stat_.get([this]() -> StatType* {
      StatType* stat = nullptr;
      IterateFn<StatType> check_stat = [this,
                                        &stat](const RefcountPtr<StatType>& shared_stat) -> bool {
        if (shared_stat->name() == name_) {
          stat = shared_stat.get();
          return false; // Stop iteration.
        }
        return true;
      };
      scope_.iterate(check_stat);
      return stat;
    });
    if (stat == nullptr) {
      return absl::nullopt;
    }
    return *stat;
  }

private:
  Scope& scope_;
  const std::string name_;
  Thread::AtomicPtr<StatType, Thread::AtomicPtrAllocMode::DoNotDelete> stat_;
};

} // namespace Stats
} // namespace Envoy
