#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

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
   * @param SymbolVec& the vector of symbols to be freed.
   * @return bool whether or not the total free operation was successful. Expected to be true.
   */
  void free(const SymbolVec& symbol_vec);

  /**
   * Convenience function for encode(), symbolizing one string segment at a time.
   *
   * @param std::string& the individual string to be encoded as a symbol.
   * @return Symbol the encoded string.
   */
  Symbol toSymbol(const std::string& str);

  /**
   * Convenience function for decode(), decoding one symbol at a time.
   *
   * @param Symbol the individual symbol to be decoded.
   * @return std::string the decoded string.
   */
  std::string fromSymbol(const Symbol symbol) const;

  Symbol curr_counter_{};

  // Bimap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  std::unordered_map<std::string, std::pair<Symbol, uint32_t>> encode_map_;
  std::unordered_map<Symbol, std::string> decode_map_;
};

/**
 * Implements RAII for Symbols, since the StatName destructor does the work of freeing its component
 * symbols.
 */
class StatNameImpl : public StatName {
public:
  StatNameImpl(SymbolVec symbol_vec, SymbolTableImpl* symbol_table)
      : symbol_vec_(symbol_vec), symbol_table_(symbol_table) {}
  ~StatNameImpl() override { symbol_table_->free(symbol_vec_); }
  std::string toString() const override { return symbol_table_->decode(symbol_vec_); }

private:
  friend class StatNameTest;
  SymbolVec symbol_vec_;
  SymbolTableImpl* symbol_table_;
};

} // namespace Stats
} // namespace Envoy
