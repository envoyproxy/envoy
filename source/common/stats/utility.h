#pragma once

#include <optional>
#include <string>
#include <utility>

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

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
  explicit DynamicName(absl::string_view str) : absl::string_view(str) {}
};

/**
 * Represents a dynamically created stat name token based on std::string.
 * This class wrapper is used in the 'Element' variant so that call-sites
 * can express explicit intent to create dynamic stat names, which are more
 * expensive than symbolic stat names. We use dynamic stat names only for
 * building stats based on names discovered in the line of a request.
 *
 * Specifically, this class should be used only when the content of the string
 * can be changed between the object creation and usage.
 */
class DynamicSavedName : public std::string {
public:
  // This is intentionally left as an implicit conversion from string_view to
  // make call-sites easier to read, e.g.
  //    Utility::counterFromElements(*scope, {DynamicSavedName("a"), DynamicSavedName("b")});
  explicit DynamicSavedName(absl::string_view str) : std::string(str.begin(), str.end()) {}
};

/**
 * Holds either a symbolic StatName or a dynamic string, for the purpose of
 * composing a vector to pass to Utility::counterFromElements, etc. This is
 * a programming convenience to create joined stat names. It is easier to
 * call the above helpers than to use SymbolTable::join(), because the helpers
 * hide the memory management of the joined storage, and they allow easier
 * co-mingling of symbolic and dynamic stat-name components.
 */
using Element = absl::variant<StatName, DynamicName, DynamicSavedName>;
using ElementVec = absl::InlinedVector<Element, 8>;

/**
 * Bundles a pre-encoded tag-extracted name, tagged name and tags, e.g. to create a tagged
 * sub-scope with Scope::scopeFromTaggedName(). To create many tagged stats directly on a shared
 * scope, see LiteScopeHelper below.
 *
 * Neither name should have a trailing dot.
 */
class TaggedStatName {
public:
  /**
   * @param symbol_table The symbol table used to encode the names and tags.
   * @param base_name The tag-extracted name, which is used to create the tag-extracted stat
   * name.
   * @param tags The tags associated with the tagged name.
   * @param name The tagged name, which is used to create the tagged stat name. If tags is empty,
   * this is ignored and base_name is used for both forms.
   *
   * Note: the names and tags are copied into a private pool, so the caller does not need to
   * maintain their lifetime after this constructor returns.
   */
  TaggedStatName(SymbolTable& symbol_table, absl::string_view base_name, TagStringViewSpan tags,
                 absl::string_view name);

  StatName name() const { return name_; }
  StatName baseName() const { return base_name_; }
  StatNameTagSpan tags() const { return tags_; }

private:
  StatNamePool tag_pool_;
  StatName name_;
  StatName base_name_;
  StatNameTagVec tags_;
};

/**
 * A lightweight, stack-only view of a Scope that adds a prefix and tags to every stat created
 * through it. Unlike a sub-scope, the helper owns no stats: every stat is created in, and owned
 * by, the underlying scope (whose own prefix and tags still apply).
 *
 * The prefix and tags are encoded once when the helper is constructed. Creating a stat then only
 * joins the pre-encoded prefix with the leaf name, rather than concatenating and re-encoding the
 * whole name for every stat. The helper provides the stat creation methods of Scope that the
 * POOL_* macros in stats_macros.h rely on, so it can be passed to them in place of a scope:
 *
 *   Stats::LiteScopeHelper helper(scope, "http", {{"envoy.http_conn_manager_prefix", "ingress"}},
 *                             "http.ingress");
 *   MyStats stats{MY_STATS(POOL_COUNTER(helper), POOL_GAUGE(helper))};
 *
 * No name should have a leading or trailing dot. When tags are supplied, the caller must also
 * supply the flat prefix with the tag values interleaved; without tags that prefix is ignored
 * and the base prefix is used for both forms.
 *
 * NOTE: the helper is designed to live on the stack for the duration of creating a batch of
 * stats. It references the scope, which must outlive it. The string-based constructor copies the
 * names and tags, so they only need to outlive the constructor; the StatName-based constructor
 * references them, so they must outlive the helper.
 */
class LiteScopeHelper {
public:
  /**
   * Creates a helper that adds nothing; it behaves like the scope itself.
   */
  explicit LiteScopeHelper(Scope& scope) : scope_(scope) {}

  /**
   * @param scope The scope in which the stats are created.
   * @param base_prefix The tag-extracted prefix to add to the stat names.
   * @param tags The tags to add to the stats.
   * @param prefix The flat prefix with the tag values interleaved. Ignored if tags is empty.
   *
   * The names and tags are encoded into, and owned by, a private pool.
   */
  LiteScopeHelper(Scope& scope, absl::string_view base_prefix, TagStringViewSpan tags = {},
                  absl::string_view prefix = {})
      : scope_(scope), owned_prefix_(std::in_place, scope.symbolTable(), base_prefix, tags, prefix),
        base_prefix_(owned_prefix_->baseName()), prefix_(owned_prefix_->name()),
        tags_(owned_prefix_->tags()) {}

  /**
   * As above, but for a prefix and tags that are already encoded. They are referenced, not
   * copied, so the caller must keep them (and their backing storage) alive for the lifetime of
   * the helper.
   *
   * TODO(wbpcode): if a caller ever needs the helper to outlive the supplied names, add an
   * overload that copies them into a private pool instead of referencing them.
   */
  LiteScopeHelper(Scope& scope, StatName base_prefix, StatNameTagSpan tags = {},
                  StatName prefix = StatName())
      : scope_(scope), base_prefix_(base_prefix), prefix_(tags.empty() ? base_prefix : prefix),
        tags_(tags) {
    ASSERT(tags.empty() || !prefix.empty(),
           "When tags are supplied, the caller must supply the tagged prefix with the tag values "
           "interleaved.");
  }

  // The subset of the Scope stat creation methods used by the stats macros. The name is the
  // stat's leaf name, to which the helper's prefix and tags are added.
  Counter& counterFromStatName(StatName name);
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode);
  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit);
  TextReadout& textReadoutFromStatName(StatName name);
  Counter& counterFromString(absl::string_view name);
  Gauge& gaugeFromString(absl::string_view name, Gauge::ImportMode import_mode);
  Histogram& histogramFromString(absl::string_view name, Histogram::Unit unit);
  TextReadout& textReadoutFromString(absl::string_view name);

  Scope& scope() { return scope_; }
  SymbolTable& symbolTable() { return scope_.symbolTable(); }
  const SymbolTable& constSymbolTable() const { return scope_.constSymbolTable(); }

  /**
   * @return the tag-extracted prefix of this helper, excluding the scope's own prefix.
   */
  StatName basePrefix() const { return base_prefix_; }
  /**
   * @return the flat prefix of this helper, excluding the scope's own prefix.
   */
  StatName prefix() const { return prefix_; }
  /**
   * @return the tags of this helper, excluding the scope's own tags.
   */
  StatNameTagSpan tags() const { return tags_; }

private:
  // The names of a single stat: the helper's prefixes joined with the stat's leaf name.
  class FullNameJoiner {
  public:
    FullNameJoiner(const LiteScopeHelper& helper, StatName name);

    StatName baseName() const { return base_name_.statName(); }
    std::optional<StatNameTagSpan> tags() const { return tags_; }
    StatName taggedName() const { return tagged_name_.statName(); }

  private:
    StatNameJoiner base_name_;
    StatNameJoiner tagged_name_;
    std::optional<StatNameTagSpan> tags_;
  };

  Scope& scope_;
  // Owns the encoded prefix and tags for the string-based constructor; the view members below
  // point into it. Unset for the StatName-based constructors, whose views reference caller-owned
  // storage.
  const std::optional<TaggedStatName> owned_prefix_;
  const StatName base_prefix_;
  const StatName prefix_;
  const StatNameTagSpan tags_;
};

/**
 * Common stats utility routines.
 */
namespace Utility {
/**
 * ':' is a reserved char in statsd. Do a character replacement to avoid
 * costly inline translations later.
 *
 * @param name the stat name to sanitize.
 * @return the sanitized stat name.
 */
std::string sanitizeStatsName(absl::string_view name);

/**
 * Sanitizes a stat name and writes it to the provided buffer. The buffer is
 * used to hold the sanitized name, and the returned string_view points to the
 * buffer. The buffer is not modified if the name does not need sanitization.
 * @param name the stat name to sanitize.
 * @param buffer the buffer to write the sanitized name to.
 * @return a string_view pointing to the sanitized name in the buffer, or the
 * original name if no sanitization was needed.
 */
absl::string_view sanitizeStatsName(absl::string_view name, std::string& buffer);

/**
 * Finds a metric tag with the specified name.
 *
 * @param metric The metric in which the tag is expected to exist.
 * @param find_tag_name The name of the tag to search for.
 * @return The value of the tag, if found.
 */
std::optional<StatName> findTag(const Metric& metric, StatName find_tag_name);

/**
 * Creates a nested scope from a vector of StatNames which are used to create the
 * name.
 *
 * See also scopeFromElements, which is slightly slower but allows
 * passing DynamicName(string)s as names.
 *
 * @param scope The scope in which to create the counter.
 * @param elements The vector of mixed DynamicName and StatName
 * @return A scope named using the joined elements.
 */
ScopeSharedPtr scopeFromStatNames(Scope& scope, const StatNameVec& names);

/**
 * Creates a counter from a vector of tokens which are used to create the
 * name. The tokens can be specified as DynamicName or StatName. For
 * tokens specified as DynamicName, a dynamic StatName will be created. See
 * https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#dynamic-stat-tokens
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
Counter& counterFromElements(Scope& scope, const ElementVec& elements,
                             StatNameTagVectorOptConstRef tags = std::nullopt);

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
Counter& counterFromStatNames(Scope& scope, const StatNameVec& names,
                              StatNameTagVectorOptConstRef tags = std::nullopt);

/**
 * Creates a gauge from a vector of tokens which are used to create the
 * name. The tokens can be specified as DynamicName or StatName. For
 * tokens specified as DynamicName, a dynamic StatName will be created. See
 * https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#dynamic-stat-tokens
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
Gauge& gaugeFromElements(Scope& scope, const ElementVec& elements, Gauge::ImportMode import_mode,
                         StatNameTagVectorOptConstRef tags = std::nullopt);

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
Gauge& gaugeFromStatNames(Scope& scope, const StatNameVec& elements, Gauge::ImportMode import_mode,
                          StatNameTagVectorOptConstRef tags = std::nullopt);

/**
 * Creates a histogram from a vector of tokens which are used to create the
 * name. The tokens can be specified as DynamicName or StatName. For
 * tokens specified as DynamicName, a dynamic StatName will be created. See
 * https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#dynamic-stat-tokens
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
Histogram& histogramFromElements(Scope& scope, const ElementVec& elements, Histogram::Unit unit,
                                 StatNameTagVectorOptConstRef tags = std::nullopt);

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
Histogram& histogramFromStatNames(Scope& scope, const StatNameVec& elements, Histogram::Unit unit,
                                  StatNameTagVectorOptConstRef tags = std::nullopt);

/**
 * Creates a TextReadout from a vector of tokens which are used to create the
 * name. The tokens can be specified as DynamicName or StatName. For
 * tokens specified as DynamicName, a dynamic StatName will be created. See
 * https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#dynamic-stat-tokens
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
TextReadout& textReadoutFromElements(Scope& scope, const ElementVec& elements,
                                     StatNameTagVectorOptConstRef tags = std::nullopt);

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
TextReadout& textReadoutFromStatNames(Scope& scope, const StatNameVec& elements,
                                      StatNameTagVectorOptConstRef tags = std::nullopt);

} // namespace Utility

/**
 * Holds a reference to a stat by name. Note that the stat may not be created
 * yet at the time CachedReference is created. Calling get() then does a lazy
 * lookup, potentially returning std::nullopt if the stat doesn't exist yet.
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
  OptRef<StatType> get() {
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
    return makeOptRefFromPtr(stat);
  }

private:
  Scope& scope_;
  const std::string name_;
  Thread::AtomicPtr<StatType, Thread::AtomicPtrAllocMode::DoNotDelete> stat_;
};

} // namespace Stats
} // namespace Envoy
