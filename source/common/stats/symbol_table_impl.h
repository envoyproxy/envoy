#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "envoy/stats/symbol_table.h"

#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

class SymbolTableImpl : public SymbolTable {
public:
  SymbolVec encode(const std::string& name) override {
    SymbolVec symbol_vec = {};
    std::vector<std::string> segment_vec = absl::StrSplit(name, '.');
    for (const auto str : segment_vec) {
      symbol_vec.push_back(toSymbol(str));
    }
    return symbol_vec;
  }

  std::string decode(const SymbolVec& symbol_vec) const override {
    std::string name;
    for (const auto token : symbol_vec) {
      absl::StrAppend(&name, fromSymbol(token), ".");
    }
    name.pop_back();
    return name;
  }

  void free(const SymbolVec& symbol_vec) override {
    for (const Symbol symbol : symbol_vec) {
      freeSymbol(symbol);
    }
  }

  // For testing purposes only.
  size_t size() const { return sizeof(SymbolTableImpl) + encode_map_.size() + decode_map_.size(); }

private:
  Symbol curr_counter_ = 0;

  // Bimap implementation. String to token is an
  // unordered_map<string, Symbol>, token to string is a vector<string>.
  // Default behavior is to split on periods.
  std::unordered_map<std::string, Symbol> encode_map_ = {};
  std::unordered_map<std::string, uint32_t> rcount_map_ = {};
  std::unordered_map<Symbol, std::string> decode_map_ = {};

  // Per-token encoding.
  Symbol toSymbol(const std::string& str) {
    Symbol result;

    auto encode_search = encode_map_.find(str);
    if (encode_search != encode_map_.end()) {
      // The symbol exists. Return it and...
      result = encode_search->second;
      // ...asserting that it exists in rcount_map_, up its ref count.
      auto ref_search = rcount_map_.find(str);
      assert(ref_search != rcount_map_.end());
      ref_search->second++;
    } else {
      // The symbol doesn't exist.
      encode_map_.insert({str, curr_counter_});
      rcount_map_.insert({str, 1});
      decode_map_.insert({curr_counter_, str});
      result = curr_counter_;
      curr_counter_++;
    }
    return result;
  }

  std::string fromSymbol(const Symbol symbol) const {
    auto search = decode_map_.find(symbol);
    assert(search != decode_map_.end());
    return search->second;
  }

  void freeSymbol(const Symbol symbol) {
    auto decode_search = decode_map_.find(symbol);
    assert(decode_search != decode_map_.end());
    std::string token = decode_search->second;
    auto rcount_search = rcount_map_.find(token);
    assert(rcount_search != rcount_map_.end());
    rcount_map_[token]--;
    if (rcount_search->second == 0) {
      decode_map_.erase(symbol);
      encode_map_.erase(token);
      rcount_map_.erase(token);
    }
  }

}; // namespace Stats

} // namespace Stats
} // namespace Envoy
