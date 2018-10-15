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
StatNamePtr SymbolTableImpl::encode(const absl::string_view name) {
  SymbolVec symbol_vec;
  std::vector<absl::string_view> name_vec = absl::StrSplit(name, '.');
  symbol_vec.reserve(name_vec.size());
  std::transform(name_vec.begin(), name_vec.end(), std::back_inserter(symbol_vec),
                 [this](absl::string_view x) { return toSymbol(x); });

  return std::make_unique<StatNameImpl>(symbol_vec, *this);
}

std::string SymbolTableImpl::decode(const SymbolVec& symbol_vec) const {
  std::vector<absl::string_view> name;
  name.reserve(symbol_vec.size());
  std::transform(symbol_vec.begin(), symbol_vec.end(), std::back_inserter(name),
                 [this](Symbol x) { return fromSymbol(x); });
  return absl::StrJoin(name, ".");
}

void SymbolTableImpl::free(const SymbolVec& symbol_vec) {
  for (const Symbol symbol : symbol_vec) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(decode_search->second);
    ASSERT(encode_search != encode_map_.end());

    encode_search->second.ref_count_--;
    // If that was the last remaining client usage of the symbol, erase the the current
    // mappings and add the now-unused symbol to the reuse pool.
    if (encode_search->second.ref_count_ == 0) {
      decode_map_.erase(decode_search);
      encode_map_.erase(encode_search);
      pool_.push(symbol);
    }
  }
}

Symbol SymbolTableImpl::toSymbol(absl::string_view sv) {
  Symbol result;
  auto encode_find = encode_map_.find(sv);
  // If the string segment doesn't already exist,
  if (encode_find == encode_map_.end()) {
    // We create the actual string, place it in the decode_map_, and then insert a string_view
    // pointing to it in the encode_map_. This allows us to only store the string once.
    std::string str = std::string(sv);

    auto decode_insert = decode_map_.insert({next_symbol_, std::move(str)});
    ASSERT(decode_insert.second);

    auto encode_insert = encode_map_.insert({decode_insert.first->second, {next_symbol_, 1}});
    ASSERT(encode_insert.second);

    result = next_symbol_;
    newSymbol();
  } else {
    // If the insertion didn't take place, return the actual value at that location and up the
    // refcount at that location
    result = encode_find->second.symbol_;
    ++(encode_find->second.ref_count_);
  }
  return result;
}

absl::string_view SymbolTableImpl::fromSymbol(const Symbol symbol) const {
  auto search = decode_map_.find(symbol);
  ASSERT(search != decode_map_.end());
  return search->second;
}

} // namespace Stats
} // namespace Envoy
