#pragma once

#include "envoy/common/pure.h"

#include "common/common/hash.h"

namespace Envoy {
namespace Stats {

class StatName;
class SymbolEncoding;

/**
 * SymbolTable manages a namespace optimized for stats, which are typically
 * composed of arrays of "."-separated tokens, with a significant overlap
 * between the tokens. Each token is mapped to a Symbol (uint32_t) and
 * reference-counted so that no-longer-used symbols can be reclaimed.
 *
 * We use a uint8_t array to encode arrays of symbols in order to conserve
 * space, as in practice the majority of token instances in stat names draw from
 * a fairly small set of common names, typically less than 100. The format is
 * somewhat similar to UTF-8, with a variable-length array of uint8_t. See the
 * implementation for details.
 *
 * StatNameStorage can be used to manage memory for the byte-encoding. Not all
 * StatNames are backed by StatNameStorage -- the storage may be inlined into
 * another object such as HeapStatData. StaNameStorage is not fully RAII --
 * instead the owner must call free(SymbolTable&) explicitly before
 * StatNameStorage is destructed. This saves 8 bytes of storage per stat,
 * relative to holding a SymbolTable& in each StatNameStorage object.
 *
 * A StatName is a copyable and assignable reference to this storage. It does
 * not own the storage or keep it alive via reference counts; the owner must
 * ensure the backing store lives as long as the StatName.
 *
 * The underlying Symbol / SymbolVec data structures are private to the
 * impl. One side effect of the non-monotonically-increasing symbol counter is
 * that if a string is encoded, the resulting stat is destroyed, and then that
 * same string is re-encoded, it may or may not encode to the same underlying
 * symbol.
 */
class SymbolTable {
public:
  virtual ~SymbolTable() {}

  /**
   * Encodes a stat name using the symbol table, returning a SymbolEncoding. The
   * SymbolEncoding is not intended for long-term storage, but is used to help
   * allocate and StatName with the correct amount of storage.
   *
   * When a name is encoded, it bumps reference counts held in the table for
   * each symbol. The caller is responsible for creating a StatName using this
   * SymbolEncoding and ultimately disposing of it by calling
   * StatName::free(). Otherwise the symbols will leak for the lifetime of the
   * table, though they won't show up as a C++ leaks as the memory is still
   * reachable from the SymolTable.
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
   * Decodes a vector of symbols back into its period-delimited stat name.  If
   * decoding fails on any part of the symbol_vec, we release_assert and crash
   * hard, since this should never happen, and we don't want to continue running
   * with a corrupt stats set.
   *
   * @param stat_name the stat name.
   * @return std::string stringifiied stat_name.
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
   * Since SymbolTable does manual reference counting, a client of SymbolTable
   * must manually call free(symbol_vec) when it is freeing the backing store
   * for a StatName. This way, the symbol table will grow and shrink
   * dynamically, instead of being write-only.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  virtual void free(const StatName& stat_name) PURE;

  /**
   * StatName backing-store can be managed by callers in a variety of ways
   * to minimize overhead. But any persistent reference to a StatName needs
   * to hold onto its own reference-counts for all symbols. This method
   * helps callers ensure the symbol-storage is maintained for the lifetime
   * of a reference.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  virtual void incRefCount(const StatName& stat_name) PURE;

#ifndef ENVOY_CONFIG_COVERAGE
  virtual void debugPrint() const PURE;
#endif
};

using SharedSymbolTable = std::shared_ptr<SymbolTable>;

} // namespace Stats
} // namespace Envoy
