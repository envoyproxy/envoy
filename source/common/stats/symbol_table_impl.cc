#include "common/stats/symbol_table_impl.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Stats {

static const uint32_t SpilloverMask = 0x80;
static const uint32_t Low7Bits = 0x7f;

StatName::StatName(const StatName& src, SymbolTable::Storage memory) : size_and_data_(memory) {
  memcpy(memory, src.size_and_data_, src.size());
}

#ifndef ENVOY_CONFIG_COVERAGE
void StatName::debugPrint() {
  if (size_and_data_ == nullptr) {
    ENVOY_LOG_MISC(info, "Null StatName");
  } else {
    uint64_t nbytes = dataSize();
    std::string msg = absl::StrCat("dataSize=", nbytes, ":");
    for (uint64_t i = 0; i < nbytes; ++i) {
      absl::StrAppend(&msg, " ", static_cast<uint64_t>(data()[i]));
    }
    SymbolVec encoding = SymbolEncoding::decodeSymbols(data(), dataSize());
    absl::StrAppend(&msg, ", numSymbols=", encoding.size(), ":");
    for (Symbol symbol : encoding) {
      absl::StrAppend(&msg, " ", symbol);
    }
    ENVOY_LOG_MISC(info, "{}", msg);
  }
}
#endif

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

void SymbolEncoding::addStringForFakeSymbolTable(absl::string_view str) {
  if (!str.empty()) {
    vec_.resize(str.size());
    memcpy(&vec_[0], str.data(), str.size());
  }
}

SymbolVec SymbolEncoding::decodeSymbols(const SymbolTable::Storage array, uint64_t size) {
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
  ASSERT(length < StatNameMaxSize);
  *bytes++ = length & 0xff;
  *bytes++ = length >> 8;
  return bytes;
}

uint64_t SymbolEncoding::moveToStorage(SymbolTable::Storage symbol_array) {
  uint64_t sz = size();
  symbol_array = saveLengthToBytesReturningNext(sz, symbol_array);
  if (sz != 0) {
    memcpy(symbol_array, vec_.data(), sz * sizeof(uint8_t));
  }
  vec_.clear(); // Logically transfer ownership, enabling empty assert on destruct.
  return sz + StatNameSizeEncodingBytes;
}

SymbolTableImpl::SymbolTableImpl()
    // Have to be explicitly initialized, if we want to use the GUARDED_BY macro.
    : next_symbol_(0), monotonic_counter_(0) {}

SymbolTableImpl::~SymbolTableImpl() {
  // To avoid leaks into the symbol table, we expect all StatNames to be freed.
  // Note: this could potentially be short-circuited if we decide a fast exit
  // is needed in production. But it would be good to ensure clean up during
  // tests.
  ASSERT(numSymbols() == 0);
}

// TODO(ambuc): There is a possible performance optimization here for avoiding
// the encoding of IPs / numbers if they appear in stat names. We don't want to
// waste time symbolizing an integer as an integer, if we can help it.
SymbolEncoding SymbolTableImpl::encode(const absl::string_view name) {
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
    for (auto& token : tokens) {
      symbols.push_back(toSymbol(token));
    }
  }

  // Now efficiently encode the array of 32-bit symbols into a uint8_t array.
  for (Symbol symbol : symbols) {
    encoding.addSymbol(symbol);
  }
  return encoding;
}

uint64_t SymbolTableImpl::numSymbols() const {
  Thread::LockGuard lock(lock_);
  ASSERT(encode_map_.size() == decode_map_.size());
  return encode_map_.size();
}

std::string SymbolTableImpl::toString(const StatName& stat_name) const {
  return decodeSymbolVec(SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.dataSize()));
}

std::string SymbolTableImpl::decodeSymbolVec(const SymbolVec& symbols) const {
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

void SymbolTableImpl::incRefCount(const StatName& stat_name) {
  // Before taking the lock, decode the array of symbols from the SymbolTable::Storage.
  SymbolVec symbols = SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.dataSize());

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(*decode_search->second);
    ASSERT(encode_search != encode_map_.end());

    ++encode_search->second.ref_count_;
  }
}

void SymbolTableImpl::free(const StatName& stat_name) {
  // Before taking the lock, decode the array of symbols from the SymbolTable::Storage.
  SymbolVec symbols = SymbolEncoding::decodeSymbols(stat_name.data(), stat_name.dataSize());

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(*decode_search->second);
    ASSERT(encode_search != encode_map_.end());

    // If that was the last remaining client usage of the symbol, erase the
    // current mappings and add the now-unused symbol to the reuse pool.
    //
    // The "if (--EXPR.ref_count_)" pattern speeds up BM_CreateRace by 20% in
    // symbol_table_speed_test.cc, relative to breaking out the decrement into a
    // separate step, likely due to the non-trivial dereferences in EXPR.
    if (--encode_search->second.ref_count_ == 0) {
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
    // We create the actual string, place it in the decode_map_, and then insert
    // a string_view pointing to it in the encode_map_. This allows us to only
    // store the string once. We use unique_ptr so copies are not made as
    // flat_hash_map moves values around.
    auto str = std::make_unique<std::string>(std::string(sv));
    auto encode_insert = encode_map_.insert({*str, SharedSymbol(next_symbol_)});
    ASSERT(encode_insert.second);
    auto decode_insert = decode_map_.insert({next_symbol_, std::move(str)});
    ASSERT(decode_insert.second);

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

absl::string_view SymbolTableImpl::fromSymbol(const Symbol symbol) const
    EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  auto search = decode_map_.find(symbol);
  RELEASE_ASSERT(search != decode_map_.end(), "no such symbol");
  return {*search->second};
}

void SymbolTableImpl::newSymbol() EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  if (pool_.empty()) {
    next_symbol_ = ++monotonic_counter_;
  } else {
    next_symbol_ = pool_.top();
    pool_.pop();
  }
  // This should catch integer overflow for the new symbol.
  ASSERT(monotonic_counter_ != 0);
}

bool SymbolTableImpl::lessThan(const StatName& a, const StatName& b) const {
  // Constructing two temp vectors during lessThan is not strictly necessary.
  // If this becomes a performance bottleneck (e.g. during sorting), we could
  // provide an iterator-like interface for incrementally decoding the symbols
  // without allocating memory.
  SymbolVec av = SymbolEncoding::decodeSymbols(a.data(), a.dataSize());
  SymbolVec bv = SymbolEncoding::decodeSymbols(b.data(), b.dataSize());

  // Calling fromSymbol requires holding the lock, as it needs read-access to
  // the maps that are written when adding new symbols.
  Thread::LockGuard lock(lock_);
  for (uint64_t i = 0, n = std::min(av.size(), bv.size()); i < n; ++i) {
    if (av[i] != bv[i]) {
      bool ret = fromSymbol(av[i]) < fromSymbol(bv[i]);
      return ret;
    }
  }
  return av.size() < bv.size();
}

#ifndef ENVOY_CONFIG_COVERAGE
void SymbolTableImpl::debugPrint() const {
  Thread::LockGuard lock(lock_);
  std::vector<Symbol> symbols;
  for (const auto& p : decode_map_) {
    symbols.push_back(p.first);
  }
  std::sort(symbols.begin(), symbols.end());
  for (Symbol symbol : symbols) {
    std::string& token = *decode_map_.find(symbol)->second;
    const SharedSymbol& shared_symbol = encode_map_.find(token)->second;
    ENVOY_LOG_MISC(info, "{}: '{}' ({})", symbol, token, shared_symbol.ref_count_);
  }
}
#endif

StatNameStorage::StatNameStorage(absl::string_view name, SymbolTable& table) {
  SymbolEncoding encoding = table.encode(name);
  bytes_ = std::make_unique<uint8_t[]>(encoding.bytesRequired());
  encoding.moveToStorage(bytes_.get());
}

StatNameStorage::StatNameStorage(StatName src, SymbolTable& table) {
  uint64_t size = src.size();
  bytes_ = std::make_unique<uint8_t[]>(size);
  src.copyToStorage(bytes_.get());
  table.incRefCount(statName());
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

SymbolTable::StoragePtr SymbolTableImpl::join(const std::vector<StatName>& stat_names) const {
  uint64_t num_bytes = 0;
  for (StatName stat_name : stat_names) {
    num_bytes += stat_name.dataSize();
  }
  auto bytes = std::make_unique<uint8_t[]>(num_bytes + StatNameSizeEncodingBytes);
  uint8_t* p = saveLengthToBytesReturningNext(num_bytes, bytes.get());
  for (StatName stat_name : stat_names) {
    num_bytes = stat_name.dataSize();
    memcpy(p, stat_name.data(), num_bytes);
    p += num_bytes;
  }
  return bytes;
}

StatNameList::~StatNameList() { ASSERT(!populated()); }

void StatNameList::populate(const std::vector<absl::string_view>& names,
                            SymbolTable& symbol_table) {
  RELEASE_ASSERT(names.size() < 256, "Maximum number elements in a StatNameList exceeded");

  // First encode all the names.
  size_t total_size_bytes = 1; /* one byte for holding the number of names */
  std::vector<SymbolEncoding> encodings;
  encodings.resize(names.size());
  int index = 0;
  for (auto& name : names) {
    SymbolEncoding encoding = symbol_table.encode(name);
    total_size_bytes += encoding.bytesRequired();
    encodings[index++].swap(encoding);
  }

  // Now allocate the exact number of bytes required and move the encodings
  // into storage.
  storage_ = std::make_unique<uint8_t[]>(total_size_bytes);
  uint8_t* p = &storage_[0];
  *p++ = encodings.size();
  for (auto& encoding : encodings) {
    p += encoding.moveToStorage(p);
  }
  ASSERT(p == &storage_[0] + total_size_bytes);
}

void StatNameList::iterate(const std::function<bool(StatName)>& f) const {
  uint8_t* p = &storage_[0];
  uint32_t num_elements = *p++;
  for (uint32_t i = 0; i < num_elements; ++i) {
    StatName stat_name(p);
    p += stat_name.size();
    if (!f(stat_name)) {
      break;
    }
  }
}

void StatNameList::clear(SymbolTable& symbol_table) {
  iterate([&symbol_table](StatName stat_name) -> bool {
    symbol_table.free(stat_name);
    return true;
  });
  storage_.reset();
}

} // namespace Stats
} // namespace Envoy
