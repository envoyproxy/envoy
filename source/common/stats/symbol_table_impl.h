#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/non_copyable.h"
#include "common/common/stack_array.h"
#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/stats/recent_lookups.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

/** A Symbol represents a string-token with a small index. */
using Symbol = uint32_t;

/**
 * We encode the byte-size of a StatName as its first two bytes.
 */
constexpr uint64_t StatNameSizeEncodingBytes = 2;
constexpr uint64_t StatNameMaxSize = 1 << (8 * StatNameSizeEncodingBytes); // 65536

/** Transient representations of a vector of 32-bit symbols */
using SymbolVec = std::vector<Symbol>;

/**
 * SymbolTableImpl manages a namespace optimized for stats, which are typically
 * composed of arrays of "."-separated tokens, with a significant overlap
 * between the tokens. Each token is mapped to a Symbol (uint32_t) and
 * reference-counted so that no-longer-used symbols can be reclaimed.
 *
 * We use a uint8_t array to encode a "."-deliminated stat-name into arrays of
 * integer symbol IDs in order to conserve space, as in practice the
 * majority of token instances in stat names draw from a fairly small set of
 * common names, typically less than 100. The format is somewhat similar to
 * UTF-8, with a variable-length array of uint8_t. See the implementation for
 * details.
 *
 * StatNameStorage can be used to manage memory for the byte-encoding. Not all
 * StatNames are backed by StatNameStorage -- the storage may be inlined into
 * another object such as HeapStatData. StaNameStorage is not fully RAII --
 * instead the owner must call free(SymbolTable&) explicitly before
 * StatNameStorage is destructed. This saves 8 bytes of storage per stat,
 * relative to holding a SymbolTable& in each StatNameStorage object.
 *
 * A StatName is a copyable and assignable reference to this storage. It does
 * not own the storage or keep it alive via reference counts; the owner must
 * ensure the backing store lives as long as the StatName.
 *
 * The underlying Symbol / SymbolVec data structures are private to the
 * impl. One side effect of the non-monotonically-increasing symbol counter is
 * that if a string is encoded, the resulting stat is destroyed, and then that
 * same string is re-encoded, it may or may not encode to the same underlying
 * symbol.
 */
class SymbolTableImpl : public SymbolTable {
public:
  /**
   * Intermediate representation for a stat-name. This helps store multiple
   * names in a single packed allocation. First we encode each desired name,
   * then sum their sizes for the single packed allocation. This is used to
   * store MetricImpl's tags and tagExtractedName.
   */
  class Encoding {
  public:
    /**
     * Before destructing SymbolEncoding, you must call moveToStorage. This
     * transfers ownership, and in particular, the responsibility to call
     * SymbolTable::clear() on all referenced symbols. If we ever wanted
     * to be able to destruct a SymbolEncoding without transferring it
     * we could add a clear(SymbolTable&) method.
     */
    ~Encoding();

    /**
     * Encodes a token into the vec.
     *
     * @param symbol the symbol to encode.
     */
    void addSymbol(Symbol symbol);

    /**
     * Decodes a uint8_t array into a SymbolVec.
     */
    static SymbolVec decodeSymbols(const SymbolTable::Storage array, uint64_t size);

    /**
     * Returns the number of bytes required to represent StatName as a uint8_t
     * array, including the encoded size.
     */
    uint64_t bytesRequired() const { return dataBytesRequired() + StatNameSizeEncodingBytes; }

    /**
     * @return the number of uint8_t entries we collected while adding symbols.
     */
    uint64_t dataBytesRequired() const { return vec_.size(); }

    /**
     * Moves the contents of the vector into an allocated array. The array
     * must have been allocated with bytesRequired() bytes.
     *
     * @param array destination memory to receive the encoded bytes.
     * @return uint64_t the number of bytes transferred.
     */
    uint64_t moveToStorage(SymbolTable::Storage array);

  private:
    std::vector<uint8_t> vec_;
  };

  SymbolTableImpl();
  ~SymbolTableImpl() override;

  // SymbolTable
  std::string toString(const StatName& stat_name) const override;
  uint64_t numSymbols() const override;
  bool lessThan(const StatName& a, const StatName& b) const override;
  void free(const StatName& stat_name) override;
  void incRefCount(const StatName& stat_name) override;
  StoragePtr join(const StatNameVec& stat_names) const override;
  void populateList(const absl::string_view* names, uint32_t num_names,
                    StatNameList& list) override;
  StoragePtr encode(absl::string_view name) override;
  void callWithStringView(StatName stat_name,
                          const std::function<void(absl::string_view)>& fn) const override;

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const override;
#endif

  /**
   * Saves the specified length into the byte array, returning the next byte.
   * There is no guarantee that bytes will be aligned, so we can't cast to a
   * uint16_t* and assign, but must individually copy the bytes.
   *
   * @param length the length in bytes to write. Must be < StatNameMaxSize.
   * @param bytes the pointer into which to write the length.
   * @return the pointer to the next byte for writing the data.
   */
  static inline uint8_t* writeLengthReturningNext(uint64_t length, uint8_t* bytes) {
    ASSERT(length < StatNameMaxSize);
    *bytes++ = length & 0xff;
    *bytes++ = length >> 8;
    return bytes;
  }

  StatNameSetPtr makeSet(absl::string_view name) override;
  void forgetSet(StatNameSet& stat_name_set) override;
  uint64_t getRecentLookups(const RecentLookupsFn&) const override;
  void clearRecentLookups() override;
  void setRecentLookupCapacity(uint64_t capacity) override;
  uint64_t recentLookupCapacity() const override;

private:
  friend class StatName;
  friend class StatNameTest;

  struct SharedSymbol {
    SharedSymbol(Symbol symbol) : symbol_(symbol), ref_count_(1) {}

    Symbol symbol_;
    uint32_t ref_count_;
  };

  // This must be held during both encode() and free().
  mutable Thread::MutexBasicLockable lock_;

  // This must be held while updating stat_name_sets_.
  mutable Thread::MutexBasicLockable stat_name_set_mutex_;

  /**
   * Decodes a vector of symbols back into its period-delimited stat name. If
   * decoding fails on any part of the symbol_vec, we release_assert and crash
   * hard, since this should never happen, and we don't want to continue running
   * with a corrupt stats set.
   *
   * @param symbols the vector of symbols to decode.
   * @return std::string the retrieved stat name.
   */
  std::string decodeSymbolVec(const SymbolVec& symbols) const;

  /**
   * Convenience function for encode(), symbolizing one string segment at a time.
   *
   * @param sv the individual string to be encoded as a symbol.
   * @return Symbol the encoded string.
   */
  Symbol toSymbol(absl::string_view sv) EXCLUSIVE_LOCKS_REQUIRED(lock_);

  /**
   * Convenience function for decode(), decoding one symbol at a time.
   *
   * @param symbol the individual symbol to be decoded.
   * @return absl::string_view the decoded string.
   */
  absl::string_view fromSymbol(Symbol symbol) const EXCLUSIVE_LOCKS_REQUIRED(lock_);

  /**
   * Stages a new symbol for use. To be called after a successful insertion.
   */
  void newSymbol();

  /**
   * Tokenizes name, finds or allocates symbols for each token, and adds them
   * to encoding.
   *
   * @param name The name to tokenize.
   * @param encoding The encoding to write to.
   */
  void addTokensToEncoding(absl::string_view name, Encoding& encoding);

  Symbol monotonicCounter() {
    Thread::LockGuard lock(lock_);
    return monotonic_counter_;
  }

  // Stores the symbol to be used at next insertion. This should exist ahead of insertion time so
  // that if insertion succeeds, the value written is the correct one.
  Symbol next_symbol_ GUARDED_BY(lock_);

  // If the free pool is exhausted, we monotonically increase this counter.
  Symbol monotonic_counter_;

  // Bitmap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  // Using absl::string_view lets us only store the complete string once, in the decode map.
  using EncodeMap = absl::flat_hash_map<absl::string_view, SharedSymbol>;
  using DecodeMap = absl::flat_hash_map<Symbol, InlineStringPtr>;
  EncodeMap encode_map_ GUARDED_BY(lock_);
  DecodeMap decode_map_ GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ GUARDED_BY(lock_);
  RecentLookups recent_lookups_ GUARDED_BY(lock_);

  absl::flat_hash_set<StatNameSet*> stat_name_sets_ GUARDED_BY(stat_name_set_mutex_);
};

/**
 * Holds backing storage for a StatName. Usage of this is not required, as some
 * applications may want to hold multiple StatName objects in one contiguous
 * uint8_t array, or embed the characters directly in another structure.
 *
 * This is intended for embedding in other data structures that have access
 * to a SymbolTable. StatNameStorage::free(symbol_table) must be called prior
 * to allowing the StatNameStorage object to be destructed, otherwise an assert
 * will fire to guard against symbol-table leaks.
 *
 * Thus this class is inconvenient to directly use as temp storage for building
 * a StatName from a string. Instead it should be used via StatNameManagedStorage.
 */
class StatNameStorage {
public:
  // Basic constructor for when you have a name as a string, and need to
  // generate symbols for it.
  StatNameStorage(absl::string_view name, SymbolTable& table);

  // Move constructor; needed for using StatNameStorage as an
  // absl::flat_hash_map value.
  StatNameStorage(StatNameStorage&& src) noexcept : bytes_(std::move(src.bytes_)) {}

  // Obtains new backing storage for an already existing StatName. Used to
  // record a computed StatName held in a temp into a more persistent data
  // structure.
  StatNameStorage(StatName src, SymbolTable& table);

  /**
   * Before allowing a StatNameStorage to be destroyed, you must call free()
   * on it, to drop the references to the symbols, allowing the SymbolTable
   * to shrink.
   */
  ~StatNameStorage();

  /**
   * Decrements the reference counts in the SymbolTable.
   *
   * @param table the symbol table.
   */
  void free(SymbolTable& table);

  /**
   * @return StatName a reference to the owned storage.
   */
  inline StatName statName() const;

  uint8_t* bytes() { return bytes_.get(); }

private:
  SymbolTable::StoragePtr bytes_;
};

/**
 * Efficiently represents a stat name using a variable-length array of uint8_t.
 * This class does not own the backing store for this array; the backing-store
 * can be held in StatNameStorage, or it can be packed more tightly into another
 * object.
 *
 * When Envoy is configured with a large numbers of clusters, there are a huge
 * number of StatNames, so avoiding extra per-stat pointers has a significant
 * memory impact.
 */
class StatName {
public:
  // Constructs a StatName object directly referencing the storage of another
  // StatName.
  explicit StatName(const SymbolTable::Storage size_and_data) : size_and_data_(size_and_data) {}

  // Constructs an empty StatName object.
  StatName() = default;

  /**
   * Defines default hash function so StatName can be used as a key in an absl
   * hash-table without specifying a functor. See
   * https://abseil.io/docs/cpp/guides/hash for details.
   */
  template <typename H> friend H AbslHashValue(H h, StatName stat_name) {
    if (stat_name.empty()) {
      return H::combine(std::move(h), absl::string_view());
    }

    // Casts the raw data as a string_view. Note that this string_view will not
    // be in human-readable form, but it will be compatible with a string-view
    // hasher.
    const char* cdata = reinterpret_cast<const char*>(stat_name.data());
    absl::string_view data_as_string_view = absl::string_view(cdata, stat_name.dataSize());
    return H::combine(std::move(h), data_as_string_view);
  }

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying representation.
   */
  uint64_t hash() const { return absl::Hash<StatName>()(*this); }

  bool operator==(const StatName& rhs) const {
    const uint64_t sz = dataSize();
    return sz == rhs.dataSize() && memcmp(data(), rhs.data(), sz * sizeof(uint8_t)) == 0;
  }
  bool operator!=(const StatName& rhs) const { return !(*this == rhs); }

  /**
   * @return uint64_t the number of bytes in the symbol array, excluding the two-byte
   *                  overhead for the size itself.
   */
  uint64_t dataSize() const {
    if (size_and_data_ == nullptr) {
      return 0;
    }
    return size_and_data_[0] | (static_cast<uint64_t>(size_and_data_[1]) << 8);
  }

  /**
   * @return uint64_t the number of bytes in the symbol array, including the two-byte
   *                  overhead for the size itself.
   */
  uint64_t size() const { return dataSize() + StatNameSizeEncodingBytes; }

  void copyToStorage(SymbolTable::Storage storage) { memcpy(storage, size_and_data_, size()); }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

  /**
   * @return A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const { return size_and_data_ + StatNameSizeEncodingBytes; }

  /**
   * @return whether this is empty.
   */
  bool empty() const { return size_and_data_ == nullptr || dataSize() == 0; }

private:
  const uint8_t* size_and_data_{nullptr};
};

StatName StatNameStorage::statName() const { return StatName(bytes_.get()); }

/**
 * Contains the backing store for a StatName and enough context so it can
 * self-delete through RAII. This works by augmenting StatNameStorage with a
 * reference to the SymbolTable&, so it has an extra 8 bytes of footprint. It
 * is intended to be used in cases where simplicity of implementation is more
 * important than byte-savings, for example:
 *   - outside the stats system
 *   - in tests
 *   - as a scoped temp in a function
 * Due to the extra 8 bytes per instance, scalability should be taken into
 * account before using this as (say) a value or key in a map. In those
 * scenarios, it would be better to store the SymbolTable reference once
 * for the entire map.
 *
 * In the stat structures, we generally use StatNameStorage to avoid the
 * per-stat overhead.
 */
class StatNameManagedStorage : public StatNameStorage {
public:
  // Basic constructor for when you have a name as a string, and need to
  // generate symbols for it.
  StatNameManagedStorage(absl::string_view name, SymbolTable& table)
      : StatNameStorage(name, table), symbol_table_(table) {}

  ~StatNameManagedStorage() { free(symbol_table_); }

private:
  SymbolTable& symbol_table_;
};

/**
 * Maintains storage for a collection of StatName objects. Like
 * StatNameManagedStorage, this has an RAII usage model, taking
 * care of decrementing ref-counts in the SymbolTable for all
 * contained StatNames on destruction or on clear();
 *
 * Example usage:
 *   StatNamePool pool(symbol_table);
 *   StatName name1 = pool.add("name1");
 *   StatName name2 = pool.add("name2");
 *   uint8_t* storage = pool.addReturningStorage("name3");
 *   StatName name3(storage);
 */
class StatNamePool {
public:
  explicit StatNamePool(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  ~StatNamePool() { clear(); }

  /**
   * Removes all StatNames from the pool.
   */
  void clear();

  /**
   * @param name the name to add the container.
   * @return the StatName held in the container for this name.
   */
  StatName add(absl::string_view name);

  /**
   * Does essentially the same thing as add(), but returns the storage as a
   * pointer which can later be used to create a StatName. This can be used
   * to accumulate a vector of uint8_t* which can later be used to create
   * StatName objects on demand.
   *
   * The use-case for this is in source/common/http/codes.cc, where we have a
   * fixed sized array of atomic pointers, indexed by HTTP code. This API
   * enables storing the allocated stat-name in that array of atomics, which
   * enables content-avoidance when finding StatNames for frequently used HTTP
   * codes.
   *
   * @param name the name to add the container.
   * @return a pointer to the bytes held in the container for this name, suitable for
   *         using to construct a StatName.
   */
  uint8_t* addReturningStorage(absl::string_view name);

private:
  // We keep the stat names in a vector of StatNameStorage, storing the
  // SymbolTable reference separately. This saves 8 bytes per StatName,
  // at the cost of having a destructor that calls clear().
  SymbolTable& symbol_table_;
  std::vector<StatNameStorage> storage_vector_;
};

// Represents an ordered container of StatNames. The encoding for each StatName
// is byte-packed together, so this carries less overhead than allocating the
// storage separately. The trade-off is there is no random access; you can only
// iterate through the StatNames.
//
// The maximum size of the list is 255 elements, so the length can fit in a
// byte. It would not be difficult to increase this, but there does not appear
// to be a current need.
class StatNameList {
public:
  ~StatNameList();

  /**
   * @return true if populate() has been called on this list.
   */
  bool populated() const { return storage_ != nullptr; }

  /**
   * Iterates over each StatName in the list, calling f(StatName). f() should
   * return true to keep iterating, or false to end the iteration.
   *
   * @param f The function to call on each stat.
   */
  void iterate(const std::function<bool(StatName)>& f) const;

  /**
   * Frees each StatName in the list. Failure to call this before destruction
   * results in an ASSERT at destruction of the list and the SymbolTable.
   *
   * This is not done as part of destruction as the SymbolTable may already
   * be destroyed.
   *
   * @param symbol_table the symbol table.
   */
  void clear(SymbolTable& symbol_table);

private:
  friend class FakeSymbolTableImpl;
  friend class SymbolTableImpl;

  /**
   * Moves the specified storage into the list. The storage format is an
   * array of bytes, organized like this:
   *
   * [0] The number of elements in the list (must be < 256).
   * [1] low order 8 bits of the number of symbols in the first element.
   * [2] high order 8 bits of the number of symbols in the first element.
   * [3...] the symbols in the first element.
   * ...
   *
   *
   * For FakeSymbolTableImpl, each symbol is a single char, casted into a
   * uint8_t. For SymbolTableImpl, each symbol is 1 or more bytes, in a
   * variable-length encoding. See SymbolTableImpl::Encoding::addSymbol for
   * details.
   */
  void moveStorageIntoList(SymbolTable::StoragePtr&& storage) { storage_ = std::move(storage); }

  SymbolTable::StoragePtr storage_;
};

// Value-templatized hash-map with StatName key.
template <class T> using StatNameHashMap = absl::flat_hash_map<StatName, T>;

// Hash-set of StatNames
using StatNameHashSet = absl::flat_hash_set<StatName>;

// Helper class for sorting StatNames.
struct StatNameLessThan {
  StatNameLessThan(const SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  bool operator()(const StatName& a, const StatName& b) const {
    return symbol_table_.lessThan(a, b);
  }

  const SymbolTable& symbol_table_;
};

struct HeterogeneousStatNameHash {
  // Specifying is_transparent indicates to the library infrastructure that
  // type-conversions should not be applied when calling find(), but instead
  // pass the actual types of the contained and searched-for objects directly to
  // these functors. See
  // https://en.cppreference.com/w/cpp/utility/functional/less_void for an
  // official reference, and https://abseil.io/tips/144 for a description of
  // using it in the context of absl.
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  size_t operator()(StatName a) const { return a.hash(); }
  size_t operator()(const StatNameStorage& a) const { return a.statName().hash(); }
};

struct HeterogeneousStatNameEqual {
  // See description for HeterogeneousStatNameHash::is_transparent.
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  size_t operator()(StatName a, StatName b) const { return a == b; }
  size_t operator()(const StatNameStorage& a, const StatNameStorage& b) const {
    return a.statName() == b.statName();
  }
  size_t operator()(StatName a, const StatNameStorage& b) const { return a == b.statName(); }
  size_t operator()(const StatNameStorage& a, StatName b) const { return a.statName() == b; }
};

// Encapsulates a set<StatNameStorage>. We use containment here rather than a
// 'using' alias because we need to ensure that when the set is destructed,
// StatNameStorage::free(symbol_table) is called on each entry. It is a little
// easier at the call-sites in thread_local_store.cc to implement this an
// explicit free() method, analogous to StatNameStorage::free(), compared to
// storing a SymbolTable reference in the class and doing the free in the
// destructor, like StatNameManagedStorage.
class StatNameStorageSet {
public:
  using HashSet =
      absl::flat_hash_set<StatNameStorage, HeterogeneousStatNameHash, HeterogeneousStatNameEqual>;
  using Iterator = HashSet::iterator;

  ~StatNameStorageSet();

  /**
   * Releases all symbols held in this set. Must be called prior to destruction.
   *
   * @param symbol_table The symbol table that owns the symbols.
   */
  void free(SymbolTable& symbol_table);

  /**
   * @param storage The StatNameStorage to add to the set.
   */
  std::pair<HashSet::iterator, bool> insert(StatNameStorage&& storage) {
    return hash_set_.insert(std::move(storage));
  }

  /**
   * @param stat_name The stat_name to find.
   * @return the iterator pointing to the stat_name, or end() if not found.
   */
  Iterator find(StatName stat_name) { return hash_set_.find(stat_name); }

  /**
   * @return the end-marker.
   */
  Iterator end() { return hash_set_.end(); }

  /**
   * @param set the storage set to swap with.
   */
  void swap(StatNameStorageSet& set) { hash_set_.swap(set.hash_set_); }

  /**
   * @return the number of elements in the set.
   */
  size_t size() const { return hash_set_.size(); }

private:
  HashSet hash_set_;
};

// Captures StatNames for lookup by string, keeping two maps: a map of
// 'built-ins' that is expected to be populated during initialization, and a map
// of dynamically discovered names. The latter map is protected by a mutex, and
// can be mutated at runtime.
//
// Ideally, builtins should be added during process initialization, in the
// outermost relevant context. And as the builtins map is not mutex protected,
// builtins must *not* be added in the request-path.
class StatNameSet {
public:
  // This object must be instantiated via SymbolTable::makeSet(), thus constructor is private.

  ~StatNameSet();

  /**
   * Adds a string to the builtin map, which is not mutex protected. This map is
   * always consulted first as a hit there means no lock is required.
   *
   * Builtins can only be added immediately after construction, as the builtins
   * map is not mutex-protected.
   */
  void rememberBuiltin(absl::string_view str);

  /**
   * Remembers every string in a container as builtins.
   */
  template <class StringContainer> void rememberBuiltins(const StringContainer& container) {
    for (const auto& str : container) {
      rememberBuiltin(str);
    }
  }
  void rememberBuiltins(const std::vector<const char*>& container) {
    rememberBuiltins<std::vector<const char*>>(container);
  }

  /**
   * Finds a StatName by name. If 'token' has been remembered as a built-in,
   * then no lock is required. Otherwise we must consult dynamic_stat_names_
   * under a lock that's private to the StatNameSet. If that's empty, we need to
   * create the StatName in the pool, which requires taking a global lock, and
   * then remember the new StatName in the dynamic_stat_names_. This allows
   * subsequent lookups of the same string to take only the set's lock, and not
   * the whole symbol-table lock.
   *
   * @return a StatName corresponding to the passed-in token, owned by the set.
   *
   * TODO(jmarantz): Potential perf issue here with contention, both on this
   * set's mutex and also the SymbolTable mutex which must be taken during
   * StatNamePool::add().
   */
  StatName getDynamic(absl::string_view token);

  /**
   * Finds a builtin StatName by name. If the builtin has not been registered,
   * then the fallback is returned.
   *
   * @return the StatName or fallback.
   */
  StatName getBuiltin(absl::string_view token, StatName fallback);

  /**
   * Adds a StatName using the pool, but without remembering it in any maps.
   */
  StatName add(absl::string_view str) {
    absl::MutexLock lock(&mutex_);
    return pool_.add(str);
  }

  /**
   * Clears recent lookups.
   */
  void clearRecentLookups();

  /**
   * Sets the number of names recorded in the recent-lookups set.
   *
   * @param capacity the capacity to configure.
   */
  void setRecentLookupCapacity(uint64_t capacity);

private:
  friend class FakeSymbolTableImpl;
  friend class SymbolTableImpl;

  StatNameSet(SymbolTable& symbol_table, absl::string_view name);
  uint64_t getRecentLookups(const RecentLookups::IterFn& iter) const;

  const std::string name_;
  Stats::SymbolTable& symbol_table_;
  Stats::StatNamePool pool_ GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  using StringStatNameMap = absl::flat_hash_map<std::string, Stats::StatName>;
  StringStatNameMap builtin_stat_names_;
  StringStatNameMap dynamic_stat_names_ GUARDED_BY(mutex_);
  RecentLookups recent_lookups_ GUARDED_BY(mutex_);
};

} // namespace Stats
} // namespace Envoy
