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
  std::unordered_map<std::string, std::pair<Symbol, uint32_t>> encode_map_ = {};
  std::unordered_map<Symbol, std::string> decode_map_ = {};

  Symbol toSymbol(const std::string& str) {
    Symbol result;
    auto encode_search = encode_map_.find(str);
    if (encode_search != encode_map_.end()) {
      // If the symbol exists. Return it and up its refcount.
      result = encode_search->second.first;
      (encode_search->second.second)++;
    } else {
      encode_map_.insert({str, std::make_pair(curr_counter_, 1)});
      decode_map_.insert({curr_counter_, str});
      result = curr_counter_;
      curr_counter_++;
    }
    return result;
  }

  std::string fromSymbol(const Symbol symbol) const {
    auto search = decode_map_.find(symbol);
    return (search != decode_map_.end()) ? (search->second) : "";
  }

  // Returns true if the free was successful, false if the symbol was invalid.
  bool freeSymbol(const Symbol symbol) {
    auto decode_search = decode_map_.find(symbol);
    if (decode_search == decode_map_.end()) {
      return false;
    }
    std::string str = decode_search->second;
    auto encode_search = encode_map_.find(str);
    if (encode_search == encode_map_.end()) {
      return false;
    }
    ((encode_search->second).second)--;
    if ((encode_search->second).second == 0) {
      decode_map_.erase(symbol);
      encode_map_.erase(str);
    }
    return true;
  }
};

} // namespace Stats
} // namespace Envoy
