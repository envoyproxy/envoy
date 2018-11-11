#pragma once

#include "envoy/common/pure.h"

#include "common/common/hash.h"

namespace Envoy {
namespace Stats {

/** Efficient byte-encoded storage an array of tokens, which are typically < 127 */
using SymbolStorage = uint8_t[];

class SymbolEncoding;
class SymbolTable;

/**
 * Efficiently represents a stat name using a variable-length array of uint8_t.
 * This class does not own the backing store for this array; the backing-store
 * can be held in StatNameStorage, or it can be packed more tightly into another
 * object.
 *
 * For large numbers of clusters, there are a huge number of StatNames so
 * avoiding extra per-stat pointers has a significant memory impact.
 */
class StatName {
public:
  // Constructs a StatName object directly referencing the storage of another
  // StatName.
  explicit StatName(const SymbolStorage symbol_array) : symbol_array_(symbol_array) {}

  // Constructs an empty StatName object.
  StatName() : symbol_array_(nullptr) {}

  // Constructs a StatName object with new storage, which must be of size
  // src.numBytesIncludingLenggth(). This is used in the a flow where we first
  // construct a StatName for lookup in a cache, and then on a miss need/ to
  // store the data directly.
  StatName(const StatName& src, SymbolStorage memory);

  std::string toString(const SymbolTable& table) const;

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying representation.
   */
  uint64_t hash() const {
    const char* cdata = reinterpret_cast<const char*>(data());
    return HashUtil::xxHash64(absl::string_view(cdata, numBytes()));
  }

  bool operator==(const StatName& rhs) const {
    const uint64_t sz = numBytes();
    return sz == rhs.numBytes() && memcmp(data(), rhs.data(), sz * sizeof(uint8_t)) == 0;
  }
  bool operator!=(const StatName& rhs) const { return !(*this == rhs); }

  /**
   * @return uint64_t the number of bytes in the symbol array, excluding the two-byte
   *                  overhead for the size itself.
   */
  uint64_t numBytes() const {
    return symbol_array_[0] | (static_cast<uint64_t>(symbol_array_[1]) << 8);
  }

  const uint8_t* symbolArray() const { return symbol_array_; }

  /**
   * @return uint64_t the number of bytes in the symbol array, including the two-byte
   *                  overhead for the size itself.
   */
  uint64_t numBytesIncludingLength() const { return numBytes() + 2; }

  void copyToStorage(SymbolStorage storage) {
    memcpy(storage, symbol_array_, numBytesIncludingLength());
  }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

  /**
   * @return uint8_t* A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const { return symbol_array_ + 2; }

protected:
  /*
  friend SymbolTable;
  friend class StatNameTest;
  friend class StatNameJoiner;
  friend class StatNameStorage;
  */

  const uint8_t* symbol_array_;
};

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
  virtual void free(StatName stat_name) PURE;

  /**
   * StatName backing-store can be managed by callers in a variety of ways
   * to minimize overhead. But any persistent reference to a StatName needs
   * to hold onto its own reference-counts for all symbols. This method
   * helps callers ensure the symbol-storage is maintained for the lifetime
   * of a reference.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  virtual void incRefCount(StatName stat_name) PURE;

  /**
   * Decodes a vector of symbols back into its period-delimited stat name.
   * If decoding fails on any part of the symbol_vec, we release_assert and crash hard, since this
   * should never happen, and we don't want to continue running with a corrupt stats set.
   *
   * @param symbol_vec the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  virtual std::string decode(const SymbolStorage symbol_vec, uint64_t size) const PURE;

#ifndef ENVOY_CONFIG_COVERAGE
  virtual void debugPrint() const PURE;
#endif

  virtual bool interoperable(const SymbolTable& other) const PURE;
};

using SharedSymbolTable = std::shared_ptr<SymbolTable>;

} // namespace Stats
} // namespace Envoy
