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
    const uint64_t nbytes = dataSize();
    std::string msg = absl::StrCat("dataSize=", nbytes, ":");
    for (uint64_t i = 0; i < nbytes; ++i) {
      absl::StrAppend(&msg, " ", static_cast<uint64_t>(data()[i]));
    }
    const SymbolVec encoding = SymbolTableImpl::Encoding::decodeSymbols(data(), dataSize());
    absl::StrAppend(&msg, ", numSymbols=", encoding.size(), ":");
    for (Symbol symbol : encoding) {
      absl::StrAppend(&msg, " ", symbol);
    }
    ENVOY_LOG_MISC(info, "{}", msg);
  }
}
#endif

SymbolTableImpl::Encoding::~Encoding() {
  // Verifies that moveToStorage() was called on this encoding. Failure
  // to call moveToStorage() will result in leaks symbols.
  ASSERT(vec_.empty());
}

void SymbolTableImpl::Encoding::addSymbol(Symbol symbol) {
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

SymbolVec SymbolTableImpl::Encoding::decodeSymbols(const SymbolTable::Storage array,
                                                   uint64_t size) {
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

uint64_t SymbolTableImpl::Encoding::moveToStorage(SymbolTable::Storage symbol_array) {
  const uint64_t sz = dataBytesRequired();
  symbol_array = writeLengthReturningNext(sz, symbol_array);
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
void SymbolTableImpl::addTokensToEncoding(const absl::string_view name, Encoding& encoding) {
  if (name.empty()) {
    return;
  }

  // We want to hold the lock for the minimum amount of time, so we do the
  // string-splitting and prepare a temp vector of Symbol first.
  const std::vector<absl::string_view> tokens = absl::StrSplit(name, '.');
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
}

uint64_t SymbolTableImpl::numSymbols() const {
  Thread::LockGuard lock(lock_);
  ASSERT(encode_map_.size() == decode_map_.size());
  return encode_map_.size();
}

std::string SymbolTableImpl::toString(const StatName& stat_name) const {
  return decodeSymbolVec(Encoding::decodeSymbols(stat_name.data(), stat_name.dataSize()));
}

void SymbolTableImpl::callWithStringView(StatName stat_name,
                                         const std::function<void(absl::string_view)>& fn) const {
  fn(toString(stat_name));
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
  const SymbolVec symbols = Encoding::decodeSymbols(stat_name.data(), stat_name.dataSize());

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(decode_search->second->toStringView());
    ASSERT(encode_search != encode_map_.end());

    ++encode_search->second.ref_count_;
  }
}

void SymbolTableImpl::free(const StatName& stat_name) {
  // Before taking the lock, decode the array of symbols from the SymbolTable::Storage.
  const SymbolVec symbols = Encoding::decodeSymbols(stat_name.data(), stat_name.dataSize());

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(decode_search->second->toStringView());
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
    InlineStringPtr str = InlineString::create(sv);
    auto encode_insert = encode_map_.insert({str->toStringView(), SharedSymbol(next_symbol_)});
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
  return search->second->toStringView();
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
  const SymbolVec av = Encoding::decodeSymbols(a.data(), a.dataSize());
  const SymbolVec bv = Encoding::decodeSymbols(b.data(), b.dataSize());

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
    const InlineString& token = *decode_map_.find(symbol)->second;
    const SharedSymbol& shared_symbol = encode_map_.find(token.toStringView())->second;
    ENVOY_LOG_MISC(info, "{}: '{}' ({})", symbol, token.toStringView(), shared_symbol.ref_count_);
  }
}
#endif

SymbolTable::StoragePtr SymbolTableImpl::encode(absl::string_view name) {
  Encoding encoding;
  addTokensToEncoding(name, encoding);
  auto bytes = std::make_unique<Storage>(encoding.bytesRequired());
  encoding.moveToStorage(bytes.get());
  return bytes;
}

StatNameStorage::StatNameStorage(absl::string_view name, SymbolTable& table)
    : bytes_(table.encode(name)) {}

StatNameStorage::StatNameStorage(StatName src, SymbolTable& table) {
  const uint64_t size = src.size();
  bytes_ = std::make_unique<SymbolTable::Storage>(size);
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

void StatNamePool::clear() {
  for (StatNameStorage& storage : storage_vector_) {
    storage.free(symbol_table_);
  }
  storage_vector_.clear();
}

uint8_t* StatNamePool::addReturningStorage(absl::string_view str) {
  storage_vector_.push_back(Stats::StatNameStorage(str, symbol_table_));
  return storage_vector_.back().bytes();
}

StatName StatNamePool::add(absl::string_view str) { return StatName(addReturningStorage(str)); }

StatNameStorageSet::~StatNameStorageSet() {
  // free() must be called before destructing StatNameStorageSet to decrement
  // references to all symbols.
  ASSERT(hash_set_.empty());
}

void StatNameStorageSet::free(SymbolTable& symbol_table) {
  // We must free() all symbols referenced in the set, otherwise the symbols
  // will leak when the flat_hash_map superclass is destructed. They cannot
  // self-destruct without an explicit free() as each individual StatNameStorage
  // object does not have a reference to the symbol table, which would waste 8
  // bytes per stat-name. The easiest way to safely free all the contents of the
  // symbol table set is to use flat_hash_map::extract(), which removes and
  // returns an element from the set without destructing the element
  // immediately. This gives us a chance to call free() on each one before they
  // are destroyed.
  //
  // There's a performance risk here, if removing elements via
  // flat_hash_set::begin() is inefficient to use in a loop like this. One can
  // imagine a hash-table implementation where the performance of this
  // usage-model would be poor. However, tests with 100k elements appeared to
  // run quickly when compiled for optimization, so at present this is not a
  // performance issue.

  while (!hash_set_.empty()) {
    auto storage = hash_set_.extract(hash_set_.begin());
    storage.value().free(symbol_table);
  }
}

SymbolTable::StoragePtr SymbolTableImpl::join(const std::vector<StatName>& stat_names) const {
  uint64_t num_bytes = 0;
  for (StatName stat_name : stat_names) {
    num_bytes += stat_name.dataSize();
  }
  auto bytes = std::make_unique<Storage>(num_bytes + StatNameSizeEncodingBytes);
  uint8_t* p = writeLengthReturningNext(num_bytes, bytes.get());
  for (StatName stat_name : stat_names) {
    const uint64_t stat_name_bytes = stat_name.dataSize();
    memcpy(p, stat_name.data(), stat_name_bytes);
    p += stat_name_bytes;
  }
  return bytes;
}

void SymbolTableImpl::populateList(const absl::string_view* names, uint32_t num_names,
                                   StatNameList& list) {
  RELEASE_ASSERT(num_names < 256, "Maximum number elements in a StatNameList exceeded");

  // First encode all the names.
  size_t total_size_bytes = 1; /* one byte for holding the number of names */

  STACK_ARRAY(encodings, Encoding, num_names);
  for (uint32_t i = 0; i < num_names; ++i) {
    Encoding& encoding = encodings[i];
    addTokensToEncoding(names[i], encoding);
    total_size_bytes += encoding.bytesRequired();
  }

  // Now allocate the exact number of bytes required and move the encodings
  // into storage.
  auto storage = std::make_unique<Storage>(total_size_bytes);
  uint8_t* p = &storage[0];
  *p++ = num_names;
  for (auto& encoding : encodings) {
    p += encoding.moveToStorage(p);
  }

  // This assertion double-checks the arithmetic where we computed
  // total_size_bytes. After appending all the encoded data into the
  // allocated byte array, we should wind up with a pointer difference of
  // total_size_bytes from the beginning of the allocation.
  ASSERT(p == &storage[0] + total_size_bytes);
  list.moveStorageIntoList(std::move(storage));
}

StatNameList::~StatNameList() { ASSERT(!populated()); }

void StatNameList::iterate(const std::function<bool(StatName)>& f) const {
  const uint8_t* p = &storage_[0];
  const uint32_t num_elements = *p++;
  for (uint32_t i = 0; i < num_elements; ++i) {
    const StatName stat_name(p);
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
