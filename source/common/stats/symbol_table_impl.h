#pragma once

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/common/utility.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

using Symbol = uint32_t;
using SymbolVec = std::vector<Symbol>;
class StatName; // forward declaration
using StatNamePtr = std::unique_ptr<StatName>;

/**
 * Underlying SymbolTable implementation which manages per-symbol reference counting.
 *
 * The underlying Symbol / SymbolVec data structures are private to the impl. One side
 * effect of the non-monotonically-increasing symbol counter is that if a string is encoded, the
 * resulting stat is destroyed, and then that same string is re-encoded, it may or may not encode to
 * the same underlying symbol.
 */
class SymbolTable {
public:
  StatNamePtr encode(absl::string_view name);

  // For testing purposes only.
  size_t size() const {
    ASSERT(encode_map_.size() == decode_map_.size());
    return encode_map_.size();
  }

private:
  friend class StatName;
  friend class StatNameTest;

  struct SharedSymbol {
    Symbol symbol_;
    uint32_t ref_count_;
  };

  /**
   * Decodes a vector of symbols back into its period-delimited stat name.
   * If decoding fails on any part of the symbol_vec, we release_assert and crash hard, since this
   * should never happen, and we don't want to continue running with a corrupt stats set.
   *
   * @param symbol_vec the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  std::string decode(const SymbolVec& symbol_vec) const;

  /**
   * Since SymbolTable does manual reference counting, a client of SymbolTable (such as
   * StatName) must manually call free(symbol_vec) when it is freeing the stat it represents. This
   * way, the symbol table will grow and shrink dynamically, instead of being write-only.
   *
   * @param symbol_vec the vector of symbols to be freed.
   */
  void free(const SymbolVec& symbol_vec);

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
  void newSymbol() {
    if (pool_.empty()) {
      next_symbol_ = ++monotonic_counter_;
    } else {
      next_symbol_ = pool_.top();
      pool_.pop();
    }
    // This should catch integer overflow for the new symbol.
    ASSERT(monotonic_counter_ != 0);
  }

  Symbol monotonicCounter() { return monotonic_counter_; }

  // Stores the symbol to be used at next insertion. This should exist ahead of insertion time so
  // that if insertion succeeds, the value written is the correct one.
  Symbol next_symbol_ = 0 GUARDED_BY(lock_);

  // If the free pool is exhausted, we monotonically increase this counter.
  Symbol monotonic_counter_ = 0 GUARDED_BY(lock_);

  // Bimap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  // Using absl::string_view lets us only store the complete string once, in the decode map.
  std::unordered_map<absl::string_view, SharedSymbol, StringViewHash> encode_map_ GUARDED_BY(lock_);
  std::unordered_map<Symbol, std::string> decode_map_ GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ GUARDED_BY(lock_);

  // This must be called during both encode() and free().
  mutable Thread::MutexBasicLockable lock_;
};

/**
 * Implements RAII for Symbols, since the StatName destructor does the work of freeing its component
 * symbols.
 */
class StatName {
public:
  StatName(SymbolVec symbol_vec, SymbolTable& symbol_table)
      : symbol_vec_(symbol_vec), symbol_table_(symbol_table) {}
  ~StatName() { symbol_table_.free(symbol_vec_); }
  std::string toString() const { return symbol_table_.decode(symbol_vec_); }

  // Returns a hash of the underlying symbol vector, since StatNames are uniquely defined by their
  // symbol vectors.
  uint64_t hash() const { return HashUtil::hashVector(symbol_vec_); }
  // Compares on the underlying symbol vectors.
  // NB: operator==(std::vector) checks size first, then compares equality for each element.
  bool operator==(const StatName& rhs) const { return symbol_vec_ == rhs.symbol_vec_; }

private:
  friend class StatNameTest;
  SymbolVec symbolVec() { return symbol_vec_; }
  SymbolVec symbol_vec_;
  SymbolTable& symbol_table_;
};

struct StatNamePtrHash {
  size_t operator()(const StatName* a) const { return a->hash(); }
};

struct StatNamePtrCompare {
  bool operator()(const StatName* a, const StatName* b) const {
    // This extracts the underlying statnames.
    return (*a == *b);
  }
};

struct StatNameUniquePtrHash {
  size_t operator()(const StatNamePtr& a) const { return a->hash(); }
};

struct StatNameUniquePtrCompare {
  bool operator()(const StatNamePtr& a, const StatNamePtr& b) const {
    // This extracts the underlying statnames.
    return (*a == *b);
  }
};

} // namespace Stats
} // namespace Envoy
