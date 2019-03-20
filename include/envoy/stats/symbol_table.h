#pragma once

#include <memory>
#include <vector>

#include "envoy/common/pure.h"

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

/**
 * Intermediate representation for a stat-name. This helps store multiple names
 * in a single packed allocation. First we encode each desired name, then sum
 * their sizes for the single packed allocation. This is used to store
 * MetricImpl's tags and tagExtractedName. Like StatName, we don't want to pay
 * a vptr overhead per object, and the representation is shared between the
 * SymbolTable implementations, so this is just a pre-declare.
 */
class SymbolEncoding;

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
   * Encodes a stat name using the symbol table, returning a SymbolEncoding. The
   * SymbolEncoding is not intended for long-term storage, but is used to help
   * allocate a StatName with the correct amount of storage.
   *
   * When a name is encoded, it bumps reference counts held in the table for
   * each symbol. The caller is responsible for creating a StatName using this
   * SymbolEncoding and ultimately disposing of it by calling
   * SymbolTable::free(). Users are protected from leaking symbols into the pool
   * by ASSERTions in the SymbolTable destructor.
   *
   * @param name The name to encode.
   * @return SymbolEncoding the encoded symbols.
   */
  virtual SymbolEncoding encode(absl::string_view name) PURE;

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
   * Deterines whether one StatName lexically precedes another. Note that
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
   * The caveat is that this representation does not bump reference counts on
   * the referenced Symbols in the SymbolTable, so it's only valid as long for
   * the lifetime of the joined StatNames.
   *
   * This is intended for use doing cached name lookups of scoped stats, where
   * the scope prefix and the names to combine it with are already in StatName
   * form. Using this class, they can be combined without accessing the
   * SymbolTable or, in particular, taking its lock.
   *
   * @param stat_names the names to join.
   * @return Storage allocated for the joined name.
   */
  virtual StoragePtr join(const std::vector<StatName>& stat_names) const PURE;

#ifndef ENVOY_CONFIG_COVERAGE
  virtual void debugPrint() const PURE;
#endif

private:
  friend class StatNameStorage;
  friend class StatNameList;

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
};

using SharedSymbolTable = std::shared_ptr<SymbolTable>;

} // namespace Stats
} // namespace Envoy
