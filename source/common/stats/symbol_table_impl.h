#pragma once

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

using Symbol = uint32_t;
using SymbolVec = std::vector<Symbol>;

/**
 * Underlying SymbolTableImpl implementation which manages per-symbol reference counting.
 *
 * The underlying Symbol / SymbolVec data structures are private to the impl. One side
 * effect of the non-monotonically-increasing symbol counter is that if a string is encoded, the
 * resulting stat is destroyed, and then that same string is re-encoded, it may or may not encode to
 * the same underlying symbol.
 */
class SymbolTableImpl : public SymbolTable {
public:
  StatNamePtr encode(const std::string& name) override;

  // For testing purposes only.
  size_t size() const override {
    RELEASE_ASSERT(encode_map_.size() == decode_map_.size(), "");
    return encode_map_.size();
  }

private:
  friend class StatNameImpl;
  friend class StatNameTest;
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
   * Since SymbolTableImpl does manual reference counting, a client of SymbolTable (such as
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
  absl::string_view fromSymbol(const Symbol symbol) const;

  // Stages a new symbol for use. To be called after a successful insertion.
  void newSymbol() {
    if (pool_.empty()) {
      RELEASE_ASSERT(monotonic_counter_ < std::numeric_limits<Symbol>::max(), "");
      current_symbol_ = ++monotonic_counter_;
    } else {
      current_symbol_ = pool_.top();
      pool_.pop();
    }
  }

  Symbol monotonicCounter() { return monotonic_counter_; }

  // Stores the symbol to be used at next insertion. This should exist ahead of insertion time so
  // that if insertion succeeds, the value written is the correct one.
  Symbol current_symbol_ = 0;

  // If the free pool is exhausted, we monotonically increase this counter.
  Symbol monotonic_counter_ = 0;

  // Bimap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  // Using absl::string_view lets us only store the complete string once, in the decode map.
  std::unordered_map<absl::string_view, std::pair<Symbol, uint32_t>, StringViewHash> encode_map_;
  std::unordered_map<Symbol, std::string> decode_map_;

  // Free pool of symbols for re-use.
  std::stack<Symbol> pool_;
};

/**
 * Implements RAII for Symbols, since the StatName destructor does the work of freeing its component
 * symbols.
 */
class StatNameImpl : public StatName {
public:
  StatNameImpl(SymbolVec symbol_vec, SymbolTableImpl& symbol_table)
      : symbol_vec_(symbol_vec), symbol_table_(symbol_table) {}
  ~StatNameImpl() override { symbol_table_.free(symbol_vec_); }
  std::string toString() const override { return symbol_table_.decode(symbol_vec_); }

private:
  friend class StatNameTest;
  SymbolVec symbolVec() { return symbol_vec_; }
  SymbolVec symbol_vec_;
  SymbolTableImpl& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
