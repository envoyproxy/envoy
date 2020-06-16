#include "common/stats/symbol_table_impl.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Stats {

// Masks used for variable-length encoding of arbitrary-sized integers into a
// uint8-array. The integers are typically small, so we try to store them in as
// few bytes as possible. The bottom 7 bits hold values, and the top bit is used
// to determine whether another byte is needed for more data.
static constexpr uint32_t SpilloverMask = 0x80;
static constexpr uint32_t Low7Bits = 0x7f;

// When storing Symbol arrays, we disallow Symbol 0, which is the only Symbol
// that will decode into uint8_t array starting (and ending) with {0}. Thus we
// can use a leading 0 in the first byte to indicate that what follows is
// literal char data, rather than symbol-table entries. Literal char data is
// used for dynamically discovered stat-name tokens where you don't want to take
// a symbol table lock, and would rather pay extra memory overhead to store the
// tokens as fully elaborated strings.
static constexpr Symbol FirstValidSymbol = 1;
static constexpr uint8_t LiteralStringIndicator = 0;

uint64_t StatName::dataSize() const {
  if (size_and_data_ == nullptr) {
    return 0;
  }
  return SymbolTableImpl::Encoding::decodeNumber(size_and_data_).first;
}

#ifndef ENVOY_CONFIG_COVERAGE
void StatName::debugPrint() {
  // TODO(jmarantz): capture this functionality (always prints regardless of
  // loglevel) in an ENVOY_LOG macro variant or similar, perhaps
  // ENVOY_LOG_MISC(stderr, ...);
  if (size_and_data_ == nullptr) {
    std::cerr << "Null StatName" << std::endl;
  } else {
    const uint64_t nbytes = dataSize();
    std::cerr << "dataSize=" << nbytes << ":";
    for (uint64_t i = 0; i < nbytes; ++i) {
      std::cerr << " " << static_cast<uint64_t>(data()[i]);
    }
    const SymbolVec encoding = SymbolTableImpl::Encoding::decodeSymbols(data(), dataSize());
    std::cerr << ", numSymbols=" << encoding.size() << ":";
    for (Symbol symbol : encoding) {
      std::cerr << " " << symbol;
    }
    std::cerr << std::endl;
  }
}
#endif

SymbolTableImpl::Encoding::~Encoding() {
  // Verifies that moveToMemBlock() was called on this encoding. Failure
  // to call moveToMemBlock() will result in leaks symbols.
  ASSERT(mem_block_.capacity() == 0);
}

uint64_t SymbolTableImpl::Encoding::encodingSizeBytes(uint64_t number) {
  uint64_t num_bytes = 0;
  do {
    ++num_bytes;
    number >>= 7;
  } while (number != 0);
  return num_bytes;
}

void SymbolTableImpl::Encoding::appendEncoding(uint64_t number,
                                               MemBlockBuilder<uint8_t>& mem_block) {
  // UTF-8-like encoding where a value 127 or less gets written as a single
  // byte. For higher values we write the low-order 7 bits with a 1 in
  // the high-order bit. Then we right-shift 7 bits and keep adding more bytes
  // until we have consumed all the non-zero bits in symbol.
  //
  // When decoding, we stop consuming uint8_t when we see a uint8_t with
  // high-order bit 0.
  do {
    if (number < (1 << 7)) {
      mem_block.appendOne(number); // number <= 127 gets encoded in one byte.
    } else {
      mem_block.appendOne((number & Low7Bits) | SpilloverMask); // >= 128 need spillover bytes.
    }
    number >>= 7;
  } while (number != 0);
}

void SymbolTableImpl::Encoding::addSymbols(const std::vector<Symbol>& symbols) {
  ASSERT(data_bytes_required_ == 0);
  for (Symbol symbol : symbols) {
    data_bytes_required_ += encodingSizeBytes(symbol);
  }
  mem_block_.setCapacity(data_bytes_required_);
  for (Symbol symbol : symbols) {
    appendEncoding(symbol, mem_block_);
  }
}

std::pair<uint64_t, uint64_t> SymbolTableImpl::Encoding::decodeNumber(const uint8_t* encoding) {
  uint64_t number = 0;
  uint64_t uc = SpilloverMask;
  const uint8_t* start = encoding;
  for (uint32_t shift = 0; (uc & SpilloverMask) != 0; ++encoding, shift += 7) {
    uc = static_cast<uint32_t>(*encoding);
    number |= (uc & Low7Bits) << shift;
  }
  return std::make_pair(number, encoding - start);
}

SymbolVec SymbolTableImpl::Encoding::decodeSymbols(const SymbolTable::Storage array,
                                                   uint64_t size) {
  SymbolVec symbol_vec;
  symbol_vec.reserve(size);
  decodeTokens(
      array, size, [&symbol_vec](Symbol symbol) { symbol_vec.push_back(symbol); },
      [](absl::string_view) {});
  return symbol_vec;
}

void SymbolTableImpl::Encoding::decodeTokens(
    const SymbolTable::Storage array, uint64_t size,
    const std::function<void(Symbol)>& symbolTokenFn,
    const std::function<void(absl::string_view)>& stringViewTokenFn) {
  while (size > 0) {
    if (*array == LiteralStringIndicator) {
      // To avoid scanning memory to find the literal size during decode, we
      // var-length encode the size of the literal string prior to the data.
      ASSERT(size > 1);
      ++array;
      --size;
      std::pair<uint64_t, uint64_t> length_consumed = decodeNumber(array);
      uint64_t length = length_consumed.first;
      array += length_consumed.second;
      size -= length_consumed.second;
      ASSERT(size >= length);
      stringViewTokenFn(absl::string_view(reinterpret_cast<const char*>(array), length));
      size -= length;
      array += length;
    } else {
      std::pair<uint64_t, uint64_t> symbol_consumed = decodeNumber(array);
      symbolTokenFn(symbol_consumed.first);
      size -= symbol_consumed.second;
      array += symbol_consumed.second;
    }
  }
}

std::vector<absl::string_view> SymbolTableImpl::decodeStrings(const SymbolTable::Storage array,
                                                              uint64_t size) const {
  std::vector<absl::string_view> strings;
  Thread::LockGuard lock(lock_);
  Encoding::decodeTokens(
      array, size,
      [this, &strings](Symbol symbol)
          ABSL_NO_THREAD_SAFETY_ANALYSIS { strings.push_back(fromSymbol(symbol)); },
      [&strings](absl::string_view str) { strings.push_back(str); });
  return strings;
}

void SymbolTableImpl::Encoding::moveToMemBlock(MemBlockBuilder<uint8_t>& mem_block) {
  appendEncoding(data_bytes_required_, mem_block);
  mem_block.appendBlock(mem_block_);
  mem_block_.reset(); // Logically transfer ownership, enabling empty assert on destruct.
}

void SymbolTableImpl::Encoding::appendToMemBlock(StatName stat_name,
                                                 MemBlockBuilder<uint8_t>& mem_block) {
  const uint8_t* data = stat_name.dataIncludingSize();
  if (data == nullptr) {
    mem_block.appendOne(0);
  } else {
    mem_block.appendData(absl::MakeSpan(data, stat_name.size()));
  }
}

SymbolTableImpl::SymbolTableImpl()
    // Have to be explicitly initialized, if we want to use the ABSL_GUARDED_BY macro.
    : next_symbol_(FirstValidSymbol), monotonic_counter_(FirstValidSymbol) {}

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
    recent_lookups_.lookup(name);
    for (auto& token : tokens) {
      // TODO(jmarantz): consider using StatNameDynamicStorage for tokens with
      // length below some threshold, say 4 bytes. It might be preferable not to
      // reserve Symbols for every 3 digit number found (for example) in ipv4
      // addresses.
      symbols.push_back(toSymbol(token));
    }
  }

  // Now efficiently encode the array of 32-bit symbols into a uint8_t array.
  encoding.addSymbols(symbols);
}

uint64_t SymbolTableImpl::numSymbols() const {
  Thread::LockGuard lock(lock_);
  ASSERT(encode_map_.size() == decode_map_.size());
  return encode_map_.size();
}

std::string SymbolTableImpl::toString(const StatName& stat_name) const {
  return absl::StrJoin(decodeStrings(stat_name.data(), stat_name.dataSize()), ".");
}

void SymbolTableImpl::callWithStringView(StatName stat_name,
                                         const std::function<void(absl::string_view)>& fn) const {
  fn(toString(stat_name));
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

uint64_t SymbolTableImpl::getRecentLookups(const RecentLookupsFn& iter) const {
  uint64_t total = 0;
  absl::flat_hash_map<std::string, uint64_t> name_count_map;

  // We don't want to hold lock_ while calling the iterator, but we need it to
  // access recent_lookups_, so we buffer in name_count_map.
  {
    Thread::LockGuard lock(lock_);
    recent_lookups_.forEach(
        [&name_count_map](absl::string_view str, uint64_t count)
            ABSL_NO_THREAD_SAFETY_ANALYSIS { name_count_map[std::string(str)] += count; });
    total += recent_lookups_.total();
  }

  // Now we have the collated name-count map data: we need to vectorize and
  // sort. We define the pair with the count first as std::pair::operator<
  // prioritizes its first element over its second.
  using LookupCount = std::pair<uint64_t, absl::string_view>;
  std::vector<LookupCount> lookup_data;
  lookup_data.reserve(name_count_map.size());
  for (const auto& iter : name_count_map) {
    lookup_data.emplace_back(LookupCount(iter.second, iter.first));
  }
  std::sort(lookup_data.begin(), lookup_data.end());
  for (const LookupCount& lookup_count : lookup_data) {
    iter(lookup_count.second, lookup_count.first);
  }
  return total;
}

DynamicSpans SymbolTableImpl::getDynamicSpans(StatName stat_name) const {
  DynamicSpans dynamic_spans;

  uint32_t index = 0;
  auto record_dynamic = [&dynamic_spans, &index](absl::string_view str) {
    DynamicSpan span;
    span.first = index;
    index += std::count(str.begin(), str.end(), '.');
    span.second = index;
    ++index;
    dynamic_spans.push_back(span);
  };

  // Use decodeTokens to suss out which components of stat_name are
  // symbolic vs dynamic. The lambda that takes a Symbol is called
  // for symbolic components. The lambda called with a string_view
  // is called for dynamic components.
  //
  // Note that with fake symbol tables, the Symbol lambda is called
  // once for each character in the string, and no dynamics will
  // be recorded.
  Encoding::decodeTokens(
      stat_name.data(), stat_name.dataSize(), [&index](Symbol) { ++index; }, record_dynamic);
  return dynamic_spans;
}

void SymbolTableImpl::setRecentLookupCapacity(uint64_t capacity) {
  Thread::LockGuard lock(lock_);
  recent_lookups_.setCapacity(capacity);
}

void SymbolTableImpl::clearRecentLookups() {
  Thread::LockGuard lock(lock_);
  recent_lookups_.clear();
}

uint64_t SymbolTableImpl::recentLookupCapacity() const {
  Thread::LockGuard lock(lock_);
  return recent_lookups_.capacity();
}

StatNameSetPtr SymbolTableImpl::makeSet(absl::string_view name) {
  // make_unique does not work with private ctor, even though SymbolTableImpl is a friend.
  StatNameSetPtr stat_name_set(new StatNameSet(*this, name));
  return stat_name_set;
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
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  auto search = decode_map_.find(symbol);
  RELEASE_ASSERT(search != decode_map_.end(), "no such symbol");
  return search->second->toStringView();
}

void SymbolTableImpl::newSymbol() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
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
  // provide an iterator-like interface for incrementally comparing the tokens
  // without allocating memory.
  const std::vector<absl::string_view> av = decodeStrings(a.data(), a.dataSize());
  const std::vector<absl::string_view> bv = decodeStrings(b.data(), b.dataSize());

  for (uint64_t i = 0, n = std::min(av.size(), bv.size()); i < n; ++i) {
    if (av[i] != bv[i]) {
      const bool ret = av[i] < bv[i];
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
  name = StringUtil::removeTrailingCharacters(name, '.');
  Encoding encoding;
  addTokensToEncoding(name, encoding);
  MemBlockBuilder<uint8_t> mem_block(Encoding::totalSizeBytes(encoding.bytesRequired()));
  encoding.moveToMemBlock(mem_block);
  return mem_block.release();
}

StatNameStorage::StatNameStorage(absl::string_view name, SymbolTable& table)
    : StatNameStorageBase(table.encode(name)) {}

StatNameStorage::StatNameStorage(StatName src, SymbolTable& table) {
  const uint64_t size = src.size();
  MemBlockBuilder<uint8_t> storage(size);
  src.copyToMemBlock(storage);
  setBytes(storage.release());
  table.incRefCount(statName());
}

SymbolTable::StoragePtr SymbolTableImpl::makeDynamicStorage(absl::string_view name) {
  name = StringUtil::removeTrailingCharacters(name, '.');

  // For all StatName objects, we first have the total number of bytes in the
  // representation. But for inlined dynamic string StatName variants, we must
  // store the length of the payload separately, so that if this token gets
  // joined with others, we'll know much space it consumes until the next token.
  // So the layout is
  //   [ length-of-whole-StatName, LiteralStringIndicator, length-of-name, name ]
  // So we need to figure out how many bytes we need to represent length-of-name
  // and name.

  // payload_bytes is the total number of bytes needed to represent the
  // characters in name, plus their encoded size, plus the literal indicator.
  const uint64_t payload_bytes = SymbolTableImpl::Encoding::totalSizeBytes(name.size()) + 1;

  // total_bytes includes the payload_bytes, plus the LiteralStringIndicator, and
  // the length of those.
  const uint64_t total_bytes = SymbolTableImpl::Encoding::totalSizeBytes(payload_bytes);
  MemBlockBuilder<uint8_t> mem_block(total_bytes);

  SymbolTableImpl::Encoding::appendEncoding(payload_bytes, mem_block);
  mem_block.appendOne(LiteralStringIndicator);
  SymbolTableImpl::Encoding::appendEncoding(name.size(), mem_block);
  mem_block.appendData(absl::MakeSpan(reinterpret_cast<const uint8_t*>(name.data()), name.size()));
  ASSERT(mem_block.capacityRemaining() == 0);
  return mem_block.release();
}

StatNameStorage::~StatNameStorage() {
  // StatNameStorage is not fully RAII: you must call free(SymbolTable&) to
  // decrement the reference counts held by the SymbolTable on behalf of
  // this StatName. This saves 8 bytes of storage per stat, relative to
  // holding a SymbolTable& in each StatNameStorage object.
  ASSERT(bytes() == nullptr);
}

void StatNameStorage::free(SymbolTable& table) {
  table.free(statName());
  clear();
}

void StatNamePool::clear() {
  for (StatNameStorage& storage : storage_vector_) {
    storage.free(symbol_table_);
  }
  storage_vector_.clear();
}

const uint8_t* StatNamePool::addReturningStorage(absl::string_view str) {
  storage_vector_.push_back(Stats::StatNameStorage(str, symbol_table_));
  return storage_vector_.back().bytes();
}

StatName StatNamePool::add(absl::string_view str) { return StatName(addReturningStorage(str)); }

StatName StatNameDynamicPool::add(absl::string_view str) {
  storage_vector_.push_back(Stats::StatNameDynamicStorage(str, symbol_table_));
  return StatName(storage_vector_.back().bytes());
}

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

SymbolTable::StoragePtr SymbolTableImpl::join(const StatNameVec& stat_names) const {
  uint64_t num_bytes = 0;
  for (StatName stat_name : stat_names) {
    if (!stat_name.empty()) {
      num_bytes += stat_name.dataSize();
    }
  }
  MemBlockBuilder<uint8_t> mem_block(Encoding::totalSizeBytes(num_bytes));
  Encoding::appendEncoding(num_bytes, mem_block);
  for (StatName stat_name : stat_names) {
    stat_name.appendDataToMemBlock(mem_block);
  }
  ASSERT(mem_block.capacityRemaining() == 0);
  return mem_block.release();
}

void SymbolTableImpl::populateList(const StatName* names, uint32_t num_names, StatNameList& list) {
  RELEASE_ASSERT(num_names < 256, "Maximum number elements in a StatNameList exceeded");

  // First encode all the names.
  size_t total_size_bytes = 1; /* one byte for holding the number of names */

  for (uint32_t i = 0; i < num_names; ++i) {
    total_size_bytes += names[i].size();
  }

  // Now allocate the exact number of bytes required and move the encodings
  // into storage.
  MemBlockBuilder<uint8_t> mem_block(total_size_bytes);
  mem_block.appendOne(num_names);
  for (uint32_t i = 0; i < num_names; ++i) {
    const StatName stat_name = names[i];
    Encoding::appendToMemBlock(stat_name, mem_block);
    incRefCount(stat_name);
  }

  // This assertion double-checks the arithmetic where we computed
  // total_size_bytes. After appending all the encoded data into the
  // allocated byte array, we should have exhausted all the memory
  // we though we needed.
  ASSERT(mem_block.capacityRemaining() == 0);
  list.moveStorageIntoList(mem_block.release());
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

StatNameSet::StatNameSet(SymbolTable& symbol_table, absl::string_view name)
    : name_(std::string(name)), symbol_table_(symbol_table), pool_(symbol_table) {
  builtin_stat_names_[""] = StatName();
}

void StatNameSet::rememberBuiltin(absl::string_view str) {
  StatName stat_name;
  {
    absl::MutexLock lock(&mutex_);
    stat_name = pool_.add(str);
  }
  builtin_stat_names_[str] = stat_name;
}

StatName StatNameSet::getBuiltin(absl::string_view token, StatName fallback) {
  // If token was recorded as a built-in during initialization, we can
  // service this request lock-free.
  const auto iter = builtin_stat_names_.find(token);
  if (iter != builtin_stat_names_.end()) {
    return iter->second;
  }
  return fallback;
}

} // namespace Stats
} // namespace Envoy
