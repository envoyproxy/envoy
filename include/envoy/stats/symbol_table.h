#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Runtime representation of an encoded stat name. This is predeclared only in
 * the interface without abstract methods, because (a) the underlying class
 * representation is common to both implementations of SymbolTable, and (b)
 * we do not want or need the overhead of a vptr per StatName. The common
 * declaration for StatName is in source/common/stats/symbol_table_impl.h
 */
class StatName;
using StatNameVec = absl::InlinedVector<StatName, 8>;

class StatNameList;
class StatNameSet;

using StatNameSetPtr = std::unique_ptr<StatNameSet>;

/**
 * Holds a range of indexes indicating which parts of a stat-name are
 * dynamic. This is used to transfer stats from hot-restart parent to child,
 * retaining the same name structure.
 */
using DynamicSpan = std::pair<uint32_t, uint32_t>;
using DynamicSpans = std::vector<DynamicSpan>;

/**
 * SymbolTable manages a namespace optimized for stat names, exploiting their
 * typical composition from "."-separated tokens, with a significant overlap
 * between the tokens. The interface is designed to balance optimal storage
 * at scale with hiding details from users. We seek to provide the most abstract
 * interface possible that avoids adding per-stat overhead or taking locks in
 * the hot path.
 */
class SymbolTable {
public:
  /**
   * Efficient byte-encoded storage of an array of tokens. The most common
   * tokens are typically < 127, and are represented directly. tokens >= 128
   * spill into the next byte, allowing for tokens of arbitrary numeric value to
   * be stored. As long as the most common tokens are low-valued, the
   * representation is space-efficient. This scheme is similar to UTF-8. The
   * token ordering is dependent on the order in which stat-names are encoded
   * into the SymbolTable, which will not be optimal, but in practice appears
   * to be pretty good.
   *
   * This is exposed in the interface for the benefit of join(), which which is
   * used in the hot-path to append two stat-names into a temp without taking
   * locks. This is used then in thread-local cache lookup, so that once warm,
   * no locks are taken when looking up stats.
   */
  using Storage = uint8_t[];
  using StoragePtr = std::unique_ptr<Storage>;

  virtual ~SymbolTable() = default;

  /**
   * @return uint64_t the number of symbols in the symbol table.
   */
  virtual uint64_t numSymbols() const PURE;

  /**
   * Decodes a vector of symbols back into its period-delimited stat name. If
   * decoding fails on any part of the symbol_vec, we release_assert and crash,
   * since this should never happen, and we don't want to continue running
   * with a corrupt stats set.
   *
   * @param stat_name the stat name.
   * @return std::string stringified stat_name.
   */
  virtual std::string toString(const StatName& stat_name) const PURE;

  /**
   * Determines whether one StatName lexically precedes another. Note that
   * the lexical order may not exactly match the lexical order of the
   * elaborated strings. For example, stat-name of "-.-" would lexically
   * sort after "---" but when encoded as a StatName would come lexically
   * earlier. In practice this is unlikely to matter as those are not
   * reasonable names for Envoy stats.
   *
   * Note that this operation has to be performed with the context of the
   * SymbolTable so that the individual Symbol objects can be converted
   * into strings for lexical comparison.
   *
   * @param a the first stat name
   * @param b the second stat name
   * @return bool true if a lexically precedes b.
   */
  virtual bool lessThan(const StatName& a, const StatName& b) const PURE;

  /**
   * Joins two or more StatNames. For example if we have StatNames for {"a.b",
   * "c.d", "e.f"} then the joined stat-name matches "a.b.c.d.e.f". The
   * advantage of using this representation is that it avoids having to
   * decode/encode into the elaborated form, and does not require locking the
   * SymbolTable.
   *
   * Note that this method does not bump reference counts on the referenced
   * Symbols in the SymbolTable, so it's only valid as long for the lifetime of
   * the joined StatNames.
   *
   * This is intended for use doing cached name lookups of scoped stats, where
   * the scope prefix and the names to combine it with are already in StatName
   * form. Using this class, they can be combined without accessing the
   * SymbolTable or, in particular, taking its lock.
   *
   * @param stat_names the names to join.
   * @return Storage allocated for the joined name.
   */
  virtual StoragePtr join(const StatNameVec& stat_names) const PURE;

  /**
   * Populates a StatNameList from a list of encodings. This is not done at
   * construction time to enable StatNameList to be instantiated directly in
   * a class that doesn't have a live SymbolTable when it is constructed.
   *
   * @param names A pointer to the first name in an array, allocated by the caller.
   * @param num_names The number of names.
   * @param symbol_table The symbol table in which to encode the names.
   */
  virtual void populateList(const StatName* names, uint32_t num_names, StatNameList& list) PURE;

#ifndef ENVOY_CONFIG_COVERAGE
  virtual void debugPrint() const PURE;
#endif

  /**
   * Calls the provided function with a string-view representation of the
   * elaborated name. This is useful during the interim period when we
   * are using FakeSymbolTableImpl, to avoid an extra allocation. Once
   * we migrate to using SymbolTableImpl, this interface will no longer
   * be helpful and can be removed. The reason it's useful now is that
   * it makes up, in part, for some extra runtime overhead that is spent
   * on the SymbolTable abstraction and API, without getting full benefit
   * from the improved representation.
   *
   * TODO(#6307): Remove this when the transition from FakeSymbolTableImpl to
   * SymbolTableImpl is complete.
   *
   * @param stat_name The stat name.
   * @param fn The function to call with the elaborated stat name as a string_view.
   */
  virtual void callWithStringView(StatName stat_name,
                                  const std::function<void(absl::string_view)>& fn) const PURE;

  using RecentLookupsFn = std::function<void(absl::string_view, uint64_t)>;

  /**
   * Calls the provided function with the name of the most recently looked-up
   * symbols, including lookups on any StatNameSets, and with a count of
   * the recent lookups on that symbol.
   *
   * @param iter the function to call for every recent item.
   */
  virtual uint64_t getRecentLookups(const RecentLookupsFn& iter) const PURE;

  /**
   * Clears the recent-lookups structures.
   */
  virtual void clearRecentLookups() PURE;

  /**
   * Sets the recent-lookup capacity.
   */
  virtual void setRecentLookupCapacity(uint64_t capacity) PURE;

  /**
   * @return The configured recent-lookup tracking capacity.
   */
  virtual uint64_t recentLookupCapacity() const PURE;

  /**
   * Creates a StatNameSet.
   *
   * @param name the name of the set.
   * @return the set.
   */
  virtual StatNameSetPtr makeSet(absl::string_view name) PURE;

  /**
   * Identifies the dynamic components of a stat_name into an array of integer
   * pairs, indicating the begin/end of spans of tokens in the stat-name that
   * are created from StatNameDynamicStore or StatNameDynamicPool.
   *
   * This can be used to reconstruct the same exact StatNames in
   * StatNames::mergeStats(), to enable stat continuity across hot-restart.
   *
   * @param stat_name the input stat name.
   * @return the array of pairs indicating the bounds.
   */
  virtual DynamicSpans getDynamicSpans(StatName stat_name) const PURE;

private:
  friend struct HeapStatData;
  friend class StatNameDynamicStorage;
  friend class StatNameStorage;
  friend class StatNameList;
  friend class StatNameSet;

  // The following methods are private, but are called by friend classes
  // StatNameStorage and StatNameList, which must be friendly with SymbolTable
  // in order to manage the reference-counted symbols they own.

  /**
   * Since SymbolTable does manual reference counting, a client of SymbolTable
   * must manually call free(symbol_vec) when it is freeing the backing store
   * for a StatName. This way, the symbol table will grow and shrink
   * dynamically, instead of being write-only.
   *
   * @param stat_name the stat name.
   */
  virtual void free(const StatName& stat_name) PURE;

  /**
   * StatName backing-store can be managed by callers in a variety of ways
   * to minimize overhead. But any persistent reference to a StatName needs
   * to hold onto its own reference-counts for all symbols. This method
   * helps callers ensure the symbol-storage is maintained for the lifetime
   * of a reference.
   *
   * @param stat_name the stat name.
   */
  virtual void incRefCount(const StatName& stat_name) PURE;

  /**
   * Encodes 'name' into the symbol table. Bumps reference counts for referenced
   * symbols. The caller must manage the storage, and is responsible for calling
   * SymbolTable::free() to release the reference counts.
   *
   * @param name The name to encode.
   * @return The encoded name, transferring ownership to the caller.
   *
   */
  virtual StoragePtr encode(absl::string_view name) PURE;

  virtual StoragePtr makeDynamicStorage(absl::string_view name) PURE;
};

using SymbolTablePtr = std::unique_ptr<SymbolTable>;

} // namespace Stats
} // namespace Envoy
