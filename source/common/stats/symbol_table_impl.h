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
  friend class StatNameImpl;

public:
  StatNamePtr encode(const std::string& name) override;

  // For testing purposes only.
  size_t size() const { return sizeof(SymbolTableImpl) + encode_map_.size() + decode_map_.size(); }

private:
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
   * @return bool whether or not the total free operation was successful. Expected to be true.
   */
  void free(const SymbolVec& symbol_vec);

  Symbol curr_counter_ = 0;

  // Bimap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  std::unordered_map<std::string, std::pair<Symbol, uint32_t>> encode_map_;
  std::unordered_map<Symbol, std::string> decode_map_;

  Symbol toSymbol(const std::string& str);
  std::string fromSymbol(const Symbol symbol) const;
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
  SymbolVec toSymbols() const override { return symbol_vec_; }

private:
  SymbolVec symbol_vec_;
  SymbolTableImpl* symbol_table_;
};

} // namespace Stats
} // namespace Envoy
