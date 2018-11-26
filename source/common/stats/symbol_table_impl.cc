#include "common/stats/symbol_table_impl.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

static const uint32_t SpilloverMask = 0x80;
static const uint32_t Low7Bits = 0x7f;

SymbolEncoding::~SymbolEncoding() { ASSERT(vec_.empty()); }

void SymbolEncoding::addSymbol(Symbol symbol) {
  // UTF-8-like encoding where a value 127 or less gets written as a single
  // byte. For higher values we write the low-order 7 bits with a 1 in
  // the high-order bit. Then we right-shift 7 bits and keep adding more bytes
  // until we have consumed all the non-zero bits in symbol.
  //
  // When decoding, we stop consuming uint8_t when we see a uint8_t with
  // high-order bit 0.
  do {
    if (symbol < (1 << 7)) {
      vec_.push_back(symbol); // symbols <= 127 get encoded in one byte.
    } else {
      vec_.push_back((symbol & Low7Bits) | SpilloverMask); // symbols >= 128 need spillover bytes.
    }
    symbol >>= 7;
  } while (symbol != 0);
}

SymbolVec SymbolEncoding::decodeSymbols(const SymbolStorage array, uint64_t size) {
  SymbolVec symbol_vec;
  Symbol symbol = 0;
  for (uint32_t shift = 0; size > 0; --size, ++array) {
    uint32_t uc = static_cast<uint32_t>(*array);

    // Inverse addSymbol encoding, walking down the bytes, shifting them into
    // symbol, until a byte with a zero high order bit indicates this symbol is
    // complete and we can move to the next one.
    symbol |= (uc & Low7Bits) << shift;
    if ((uc & SpilloverMask) == 0) {
      symbol_vec.push_back(symbol);
      shift = 0;
      symbol = 0;
    } else {
      shift += 7;
    }
  }
  return symbol_vec;
}

// Saves the specified length into the byte array, returning the next byte.
// There is no guarantee that bytes will be aligned, so we can't cast to a
// uint16_t* and assign, but must individually copy the bytes.
static inline uint8_t* saveLengthToBytesReturningNext(uint64_t length, uint8_t* bytes) {
  ASSERT(length < 65536);
  *bytes++ = length & 0xff;
  *bytes++ = length >> 8;
  return bytes;
}

void SymbolEncoding::moveToStorage(SymbolStorage symbol_array) {
  uint64_t sz = size();
  symbol_array = saveLengthToBytesReturningNext(sz, symbol_array);
  if (sz != 0) {
    memcpy(symbol_array, vec_.data(), sz * sizeof(uint8_t));
  }
  vec_.clear(); // Logically transfer ownership, enabling empty assert on destruct.
}

SymbolTable::SymbolTable()
    // Have to be explicitly initialized, if we want to use the GUARDED_BY macro.
    : next_symbol_(0), monotonic_counter_(0) {}

SymbolTable::~SymbolTable() {
  // To avoid leaks into the symbol table, we expect all StatNames to be freed.
  // Note: this could potentially be short-circuited if we decide a fast exit
  // is needed in production. But it would be good to ensure clean up during
  // tests.
  ASSERT(numSymbols() == 0);
}

// TODO(ambuc): There is a possible performance optimization here for avoiding
// the encoding of IPs / numbers if they appear in stat names. We don't want to
// waste time symbolizing an integer as an integer, if we can help it.
SymbolEncoding SymbolTable::encode(const absl::string_view name) {
  SymbolEncoding encoding;

  if (name.empty()) {
    return encoding;
  }

  // We want to hold the lock for the minimum amount of time, so we do the
  // string-splitting and prepare a temp vector of Symbol first.
  std::vector<absl::string_view> tokens = absl::StrSplit(name, '.');
  std::vector<Symbol> symbols;
  symbols.reserve(tokens.size());

  // Now take the lock and populate the Symbol objects, which involves bumping
  // ref-counts in this.
  {
    Thread::LockGuard lock(lock_);
    for (absl::string_view token : tokens) {
      symbols.push_back(toSymbol(token));
    }
  }

  // Now efficiently encode the array of 32-bit symbols into a uint8_t array.
  for (Symbol symbol : symbols) {
    encoding.addSymbol(symbol);
  }
  return encoding;
}

std::string SymbolTable::decode(const SymbolStorage symbol_array, uint64_t size) const {
  return decode(SymbolEncoding::decodeSymbols(symbol_array, size));
}

std::string SymbolTable::decode(const SymbolVec& symbols) const {
  std::vector<absl::string_view> name_tokens;
  name_tokens.reserve(symbols.size());
  {
    // Hold the lock only while decoding symbols.
    Thread::LockGuard lock(lock_);
    for (Symbol symbol : symbols) {
      name_tokens.push_back(fromSymbol(symbol));
    }
  }
  return absl::StrJoin(name_tokens, ".");
}

void SymbolTable::free(StatName stat_name) {
  // Before taking the lock, decode the array of symbols from the SymbolStorage.
  SymbolVec symbols = SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.numBytes());

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
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

Symbol SymbolTable::toSymbol(absl::string_view sv) EXCLUSIVE_LOCKS_REQUIRED(lock_) {
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

absl::string_view SymbolTable::fromSymbol(const Symbol symbol) const
    EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  auto search = decode_map_.find(symbol);
  ASSERT(search != decode_map_.end());
  return search->second;
}

void SymbolTable::newSymbol() EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  if (pool_.empty()) {
    next_symbol_ = ++monotonic_counter_;
  } else {
    next_symbol_ = pool_.top();
    pool_.pop();
  }
  // This should catch integer overflow for the new symbol.
  ASSERT(monotonic_counter_ != 0);
}

bool SymbolTable::lessThan(const StatName& a, const StatName& b) const {
  // Constructing two temp vectors during lessThan is not strictly necessary.
  // If this becomes a performance bottleneck (e.g. during sorting), we could
  // provide an iterator-like interface for incrementally decoding the symbols
  // without allocating memory.
  SymbolVec av = SymbolEncoding::decodeSymbols(a.data(), a.numBytes());
  SymbolVec bv = SymbolEncoding::decodeSymbols(b.data(), b.numBytes());
  for (uint64_t i = 0, n = std::min(av.size(), bv.size()); i < n; ++i) {
    if (av[i] != bv[i]) {
      Thread::LockGuard lock(lock_);
      return fromSymbol(av[i]) < fromSymbol(bv[i]);
    }
  }
  return av.size() < bv.size();
}

StatNameStorage::StatNameStorage(absl::string_view name, SymbolTable& table) {
  SymbolEncoding encoding = table.encode(name);
  bytes_ = std::make_unique<uint8_t[]>(encoding.bytesRequired());
  encoding.moveToStorage(bytes_.get());
}

StatNameStorage::~StatNameStorage() {
  // StatNameStorage is not fully RAII: you must call free(SymbolTable&) to
  // decrement the reference counts held by the SymbolTable on behalf of
  // this StatName. This saves 8 bytes of storage per stat, relative to
  // holding a SymbolTable& in each StatNameStorage object.
  ASSERT(bytes_ == nullptr);
}

void StatNameStorage::free(SymbolTable& table) {
  table.free(statName());
  bytes_.reset();
}

StatNameJoiner::StatNameJoiner(StatName a, StatName b) {
  const uint64_t a_size = a.numBytes();
  const uint64_t b_size = b.numBytes();
  uint8_t* const p = alloc(a_size + b_size);
  memcpy(p, a.data(), a_size);
  memcpy(p + a_size, b.data(), b_size);
}

StatNameJoiner::StatNameJoiner(const std::vector<StatName>& stat_names) {
  uint64_t num_bytes = 0;
  for (StatName stat_name : stat_names) {
    num_bytes += stat_name.numBytes();
  }
  uint8_t* p = alloc(num_bytes);
  for (StatName stat_name : stat_names) {
    num_bytes = stat_name.numBytes();
    memcpy(p, stat_name.data(), num_bytes);
    p += num_bytes;
  }
}

uint8_t* StatNameJoiner::alloc(uint64_t num_bytes) {
  bytes_ = std::make_unique<uint8_t[]>(num_bytes + 2); // +2 bytes for the length up to 64k.
  return saveLengthToBytesReturningNext(num_bytes, bytes_.get());
}

} // namespace Stats
} // namespace Envoy
