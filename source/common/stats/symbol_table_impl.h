#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/non_copyable.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

#include "absl/container/flat_hash_map.h"
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
   * array, including the encoded size.
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
   * @return uint64_t the number of bytes transferred.
   */
  uint64_t moveToStorage(SymbolStorage array);

  void swap(SymbolEncoding& src) { vec_.swap(src.vec_); }

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
class SymbolTableImpl : public SymbolTable {
public:
  SymbolTableImpl();
  ~SymbolTableImpl() override;

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
  SymbolEncoding encode(absl::string_view name) override;

  /**
   * @return uint64_t the number of symbols in the symbol table.
   */
  uint64_t numSymbols() const override {
    absl::ReaderMutexLock lock(&lock_);
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
  bool lessThan(const StatName& a, const StatName& b) const override;

  /**
   * Since SymbolTable does manual reference counting, a client of SymbolTable
   * must manually call free(symbol_vec) when it is freeing the backing store
   * for a StatName. This way, the symbol table will grow and shrink
   * dynamically, instead of being write-only.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  void free(StatName stat_name) override { adjustRefCount(stat_name, -1); }

  /**
   * StatName backing-store can be managed by callers in a variety of ways
   * to minimize overhead. But any persistent reference to a StatName needs
   * to hold onto its own reference-counts for all symbols. This method
   * helps callers ensure the symbol-storage is maintained for the lifetime
   * of a reference.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  void incRefCount(StatName stat_name) override { adjustRefCount(stat_name, 1); };

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const override;
#endif

  /**
   * Decodes a vector of symbols back into its period-delimited stat name.
   * If decoding fails on any part of the symbol_vec, we release_assert and crash hard, since this
   * should never happen, and we don't want to continue running with a corrupt stats set.
   *
   * @param symbol_vec the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  std::string decode(const SymbolStorage symbol_vec, uint64_t size) const override;

  bool interoperable(const SymbolTable& other) const override { return &other == this; }

private:
  friend class StatName;
  friend class StatNameTest;

  struct SharedSymbol {
    SharedSymbol(Symbol symbol) : symbol_(symbol), ref_count_(1) {}
    /*
    SharedSymbol(const SharedSymbol& src) : symbol_(src.symbol_), ref_count_(1) {
      ASSERT(src.ref_count_ == 1);
    }
    SharedSymbol& operator=(const SharedSymbol& src) {
      if (&src != this) {
        ASSERT(src.ref_count_ == 1);
        symbol_ = src.symbol_;
        ref_count_ = 1;
      }
      return *this;
    }
    */

    Symbol symbol_;
    std::atomic<uint32_t> ref_count_;
    // uint32_t ref_count_;
  };

  // This must be called during both encode() and free().
  // mutable Thread::MutexBasicLockable lock_;]
  mutable absl::Mutex lock_;

  std::string decodeSymbolVec(const SymbolVec& symbols) const;

  void adjustRefCount(StatName stat_name, int adjustment);

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
    absl::ReaderMutexLock lock(&lock_);
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
  using EncodeMap =
      absl::flat_hash_map<absl::string_view, std::unique_ptr<SharedSymbol>, StringViewHash>;
  using DecodeMap = absl::flat_hash_map<Symbol, std::unique_ptr<char[]>>;
  EncodeMap encode_map_ GUARDED_BY(lock_);
  DecodeMap decode_map_ GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ GUARDED_BY(lock_);

  //#define TRACK_ENCODES
#ifdef TRACK_ENCODES
  using Histogram = absl::flat_hash_map<std::string, uint64_t>;
  Histogram histogram_;
#endif
};

/**
 * Joins two or more StatNames. For example if we have StatNames for {"a.b",
 * "c.d", "e.f"} then the joined stat-name matches "a.b.c.d.e.f". The advantage
 * of using this representation is that it avoids having to decode/encode
 * into the elaborted form, and does not require locking the SymbolTable.
 *
 * The caveat is that this representation does not bump reference counts on
 * for the referenced Symbols in the SymbolTable, so it's only valid as long
 * for the lifetime of the joined StatNames.
 *
 * This is intended for use doing cached name lookups of scoped stats, where
 * the scope prefix and the names to combine it with are already in StatName
 * form. Using this class, they can be combined without acessingm the
 * SymbolTable or, in particular, taking its lock.
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

/**
 * Holds backing storage for a StatName. Usage of this is not required, as some
 * applications may want to hold multiple StatName objects in one contiguous
 * uint8_t array, or embed the characters directly in another structure.
 *
 * This is intended for embedding in other data structures that have access
 * to a SymbolTable. StatNameStorage::free(symbol_table) must be called prior
 * to allowing the StatNameStorage object to be destructed, otherwise an assert
 * will fire to guard against symbol-table leaks.
 *
 * Thus this class is inconvenient to directly use as temp storage for building
 * a StatName from a string. Instead it should be used via StatNameTempStorage.
 */
class StatNameStorage {
public:
  StatNameStorage(absl::string_view name, SymbolTable& table);
  StatNameStorage(StatNameStorage&& src) : bytes_(std::move(src.bytes_)) {}
  StatNameStorage(StatName src, SymbolTable& table);

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
  StatName statName() const { return StatName(bytes_.get()); }

  /*
  template<class T> T append(StatName suffix, std::function<T(StatName)> f) {
    StatNameStorage joiner(statName(), suffix);
    f(joiner.statName());
  }
  */

private:
  std::unique_ptr<SymbolStorage> bytes_;
};

class StatNameTempStorage : public StatNameStorage {
public:
  StatNameTempStorage(absl::string_view name, SymbolTable& table)
      : StatNameStorage(name, table), symbol_table_(table) {}
  StatNameTempStorage(StatName src, SymbolTable& table)
      : StatNameStorage(src, table), symbol_table_(table) {}
  ~StatNameTempStorage() { free(symbol_table_); }

private:
  SymbolTable& symbol_table_;
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
using StatNameHashMap = absl::flat_hash_map<StatName, T, StatNameHash, StatNameCompare>;

// Hash-set of StatNames
using StatNameHashSet = absl::flat_hash_set<StatName, StatNameHash, StatNameCompare>;

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
