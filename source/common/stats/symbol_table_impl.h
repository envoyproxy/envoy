#pragma once

#include <algorithm>
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
  std::vector<Symbol> encode(const std::string& name) override {
    std::vector<Symbol> symbol_vec;
    std::vector<std::string> name_vec = absl::StrSplit(name, '.');
    symbol_vec.resize(name_vec.size());
    std::transform(name_vec.begin(), name_vec.end(), symbol_vec.begin(),
                   [this](std::string x) { return this->toSymbol(x); });
    return symbol_vec;
  }

  std::string decode(const std::vector<Symbol>& symbol_vec) const override {
    std::vector<std::string> name;
    name.resize(symbol_vec.size());
    std::transform(symbol_vec.begin(), symbol_vec.end(), name.begin(),
                   [this](Symbol x) { return this->fromSymbol(x); });
    return absl::StrJoin(name, ".");
  }

  bool free(const std::vector<Symbol>& symbol_vec) override {
    bool successful_free = true;
    for (const Symbol symbol : symbol_vec) {
      successful_free = successful_free && freeSymbol(symbol);
    }
    return successful_free;
  }

  // For testing purposes only.
  size_t size() const { return sizeof(SymbolTableImpl) + encode_map_.size() + decode_map_.size(); }

private:
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
