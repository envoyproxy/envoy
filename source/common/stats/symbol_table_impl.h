#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/non_copyable.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

/** A Symbol represents a string-token with a small index. */
using Symbol = uint32_t;

/** Efficient byte-encoded storage an array of tokens, which are typically < 127 */
using SymbolStorage = uint8_t[];

/** Transient representations of a vector of 32-bit symbols */
using SymbolVec = std::vector<Symbol>;

class StatName;

/**
 * Represents an 8-bit encoding of a vector of symbols, used as a transient
 * representation during encoding and prior to retained allocation.
 */
class SymbolEncoding {
public:
  /**
   * Before destructing SymbolEncoding, you must call moveToStorage. This
   * transfers ownership, and in particular, the responsibility to call
   * SymbolTable::clear() on all referenced symbols. If we ever wanted
   * to be able to destruct a SymbolEncoding without transferring it
   * we could add a clear(SymbolTable&) method.
   */
  ~SymbolEncoding();

  /**
   * Encodes a token into the vec.
   *
   * @param symbol the symbol to encode.
   */
  void addSymbol(Symbol symbol);

  /**
   * Decodes a uint8_t array into a SymbolVec.
   */
  static SymbolVec decodeSymbols(const SymbolStorage array, uint64_t size);

  /**
   * Returns the number of bytes required to represent StatName as a uint8_t
   * array.
   */
  uint64_t bytesRequired() const { return size() + 2 /* size encoded as 2 bytes */; }

  /**
   * Returns the number of uint8_t entries we collected while adding symbols.
   */
  uint64_t size() const { return vec_.size(); }

  /**
   * Moves the contents of the vector into an allocated array. The array
   * must have been allocated with bytesRequired() bytes.
   *
   * @param array destination memory to receive the encoded bytes.
   */
  void moveToStorage(SymbolStorage array);

private:
  std::vector<uint8_t> vec_;
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
  SymbolTable();
  ~SymbolTable();

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
  SymbolEncoding encode(absl::string_view name);

  /**
   * @return uint64_t the number of symbols in the symbol table.
   */
  uint64_t numSymbols() const {
    Thread::LockGuard lock(lock_);
    ASSERT(encode_map_.size() == decode_map_.size());
    return encode_map_.size();
  }

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
  bool lessThan(const StatName& a, const StatName& b) const;

  /**
   * Since SymbolTable does manual reference counting, a client of SymbolTable
   * must manually call free(symbol_vec) when it is freeing the backing store
   * for a StatName. This way, the symbol table will grow and shrink
   * dynamically, instead of being write-only.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  void free(StatName stat_name);

private:
  friend class StatName;
  friend class StatNameTest;

  struct SharedSymbol {
    Symbol symbol_;
    uint32_t ref_count_;
  };

  // This must be called during both encode() and free().
  mutable Thread::MutexBasicLockable lock_;

  /**
   * Decodes a vector of symbols back into its period-delimited stat name.
   * If decoding fails on any part of the symbol_vec, we release_assert and crash hard, since this
   * should never happen, and we don't want to continue running with a corrupt stats set.
   *
   * @param symbol_vec the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  std::string decode(const SymbolStorage symbol_vec, uint64_t size) const;
  std::string decode(const SymbolVec& symbols) const;

  /**
   * Convenience function for encode(), symbolizing one string segment at a time.
   *
   * @param sv the individual string to be encoded as a symbol.
   * @return Symbol the encoded string.
   */
  Symbol toSymbol(absl::string_view sv);

  /**
   * Convenience function for decode(), decoding one symbol at a time.
   *
   * @param symbol the individual symbol to be decoded.
   * @return absl::string_view the decoded string.
   */
  absl::string_view fromSymbol(Symbol symbol) const;

  // Stages a new symbol for use. To be called after a successful insertion.
  void newSymbol();

  Symbol monotonicCounter() {
    Thread::LockGuard lock(lock_);
    return monotonic_counter_;
  }

  // Stores the symbol to be used at next insertion. This should exist ahead of insertion time so
  // that if insertion succeeds, the value written is the correct one.
  Symbol next_symbol_ GUARDED_BY(lock_);

  // If the free pool is exhausted, we monotonically increase this counter.
  Symbol monotonic_counter_ GUARDED_BY(lock_);

  // Bimap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  // Using absl::string_view lets us only store the complete string once, in the decode map.
  std::unordered_map<absl::string_view, SharedSymbol, StringViewHash> encode_map_ GUARDED_BY(lock_);
  std::unordered_map<Symbol, std::string> decode_map_ GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ GUARDED_BY(lock_);
};

/**
 * Holds backing storage for a StatName. Usage of this is not required, as some
 * applications may want to hold multiple StatName objects in one contiguous
 * uint8_t array, or embed the characters directly in another structure.
 */
class StatNameStorage {
public:
  StatNameStorage(absl::string_view name, SymbolTable& table);
  StatNameStorage(StatNameStorage&& src) : bytes_(std::move(src.bytes_)) {}

  /**
   * Before allowing a StatNameStorage to be destroyed, you must call free()
   * on it, to drop the references to the symbols, allowing the SymbolTable
   * to shrink.
   */
  ~StatNameStorage();

  /**
   * Decrements the reference counts in the SymbolTable.
   *
   * @param table the symbol table.
   */
  void free(SymbolTable& table);

  /**
   * @return StatName a reference to the owned storage.
   */
  inline StatName statName() const; // implementation below.

private:
  std::unique_ptr<SymbolStorage> bytes_;
};

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
  explicit StatName(const SymbolStorage symbol_array) : symbol_array_(symbol_array) {}
  StatName() : symbol_array_(nullptr) {}

  std::string toString(const SymbolTable& table) const { return table.decode(data(), numBytes()); }

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying symbol vector, since StatNames
   *                  are uniquely defined by their symbol vectors.
   */
  uint64_t hash() const {
    const char* cdata = reinterpret_cast<const char*>(data());
    return HashUtil::xxHash64(absl::string_view(cdata, numBytes()));
  }

  // Compares on the underlying symbol vectors.
  // NB: operator==(std::vector) checks size first, then compares equality for each element.
  bool operator==(const StatName& rhs) const {
    const uint64_t sz = numBytes();
    return sz == rhs.numBytes() && memcmp(data(), rhs.data(), sz * sizeof(uint8_t)) == 0;
  }
  bool operator!=(const StatName& rhs) const { return !(*this == rhs); }

protected:
  friend SymbolTable;
  friend class StatNameTest;
  friend class StatNameJoiner;

  /**
   * @return uint64_t the number of bytes in the symbol array, excluding the two-byte
   *                  overhead for the size itself.
   */
  uint64_t numBytes() const {
    return symbol_array_[0] | (static_cast<uint64_t>(symbol_array_[1]) << 8);
  }

  /**
   * @return uint8_t* A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const { return symbol_array_ + 2; }

  const uint8_t* symbol_array_;
};

StatName StatNameStorage::statName() const { return StatName(bytes_.get()); }

/**
 * Joins two or more StatNames. For example if we have StatNames for {"a.b",
 * "c.d", "e.f"} then the joined stat-name matches "a.b.c.d.e.f". The advantage
 * of using this representation is that it avoids having to decode/encode
 * into the elaborted form, and does not require locking the SymbolTable.
 *
 * The caveat is that this representation does not bump reference counts on
 * for the referenced Symbols in the SymbolTable, so it's only valid as long
 * as the joined StatNames.
 */
class StatNameJoiner {
public:
  StatNameJoiner(StatName a, StatName b);
  StatNameJoiner(const std::vector<StatName>& stat_names);

  /**
   * @return StatName a reference to the joined StatName.
   */
  StatName statName() const { return StatName(bytes_.get()); }

private:
  uint8_t* alloc(uint64_t num_bytes);

  std::unique_ptr<SymbolStorage> bytes_;
};

// Helper class for constructing hash-tables with StatName keys.
struct StatNameHash {
  size_t operator()(const StatName& a) const { return a.hash(); }
};

// Helper class for constructing hash-tables with StatName keys.
struct StatNameCompare {
  bool operator()(const StatName& a, const StatName& b) const { return a == b; }
};

// Value-templatized hash-map with StatName key.
template <class T>
using StatNameHashMap = std::unordered_map<StatName, T, StatNameHash, StatNameCompare>;

// Helper class for sorting StatNames.
struct StatNameLessThan {
  StatNameLessThan(const SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  bool operator()(const StatName& a, const StatName& b) const {
    return symbol_table_.lessThan(a, b);
  }

  const SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
