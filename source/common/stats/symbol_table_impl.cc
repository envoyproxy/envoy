#include "common/stats/symbol_table_impl.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

// TODO(ambuc): There is a possible performance optimization here for avoiding the encoding of IPs,
// if they appear in stat names. We don't want to waste time symbolizing an integer as an integer,
// if we can help it.
StatNamePtr SymbolTableImpl::encode(const std::string& name) {
  SymbolVec symbol_vec;
  std::vector<std::string> name_vec = absl::StrSplit(name, '.');
  symbol_vec.reserve(name_vec.size());
  std::transform(name_vec.begin(), name_vec.end(), std::back_inserter(symbol_vec),
                 [this](std::string x) { return toSymbol(x); });

  return std::make_unique<StatNameImpl>(symbol_vec, this);
}

std::string SymbolTableImpl::decode(const SymbolVec& symbol_vec) const {
  std::vector<std::string> name;
  name.reserve(symbol_vec.size());
  std::transform(symbol_vec.begin(), symbol_vec.end(), std::back_inserter(name),
                 [this](Symbol x) { return fromSymbol(x); });
  return absl::StrJoin(name, ".");
}

void SymbolTableImpl::free(const SymbolVec& symbol_vec) {
  for (const Symbol symbol : symbol_vec) {
    auto decode_search = decode_map_.find(symbol);
    RELEASE_ASSERT(decode_search != decode_map_.end(), "");

    const std::string& str = decode_search->second;
    auto encode_search = encode_map_.find(str);
    RELEASE_ASSERT(encode_search != encode_map_.end(), "");

    ((encode_search->second).second)--;
    // If that was the last remaining client usage of the symbol, erase the the current
    // mappings and add the now-unused symbol to the reuse pool.
    if ((encode_search->second).second == 0) {
      decode_map_.erase(decode_search);
      encode_map_.erase(encode_search);
      pool_.push_back(symbol);
    }
  }
}

Symbol SymbolTableImpl::toSymbol(const std::string& str) {
  Symbol result;
  auto encode_insert = encode_map_.insert({str, std::make_pair(current_symbol_, 1)});
  // If the insertion took place, we mirror the insertion in the decode_map.
  if (encode_insert.second) {
    auto decode_insert = decode_map_.insert({current_symbol_, str});
    // We expect the decode_map to be in lockstep.
    RELEASE_ASSERT(decode_insert.second, "");
    result = current_symbol_;
    newSymbol();
  } else {
    // If the insertion didn't take place, return the actual value at that location
    result = (encode_insert.first)->second.first;
    // and up the refcount at that location
    ++(encode_insert.first)->second.second;
  }
  return result;
}

std::string SymbolTableImpl::fromSymbol(const Symbol symbol) const {
  auto search = decode_map_.find(symbol);
  RELEASE_ASSERT(search != decode_map_.end(), "");
  return search->second;
}

} // namespace Stats
} // namespace Envoy
