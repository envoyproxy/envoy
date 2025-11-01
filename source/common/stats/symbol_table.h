#pragma once

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/mem_block_builder.h"
#include "source/common/common/non_copyable.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/stats/recent_lookups.h"

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

class StatName;
using StatNameVec = absl::InlinedVector<StatName, 8>;
class StatNameList;
class StatNameSet;
using StatNameSetPtr = std::unique_ptr<StatNameSet>;

/**
 * Holds a range of indexes indicating which parts of a stat-name are
 * dynamic. This is used to transfer stats from hot-restart parent to child,
 * retaining the same name structure.
 */
using DynamicSpan = std::pair<uint32_t, uint32_t>;
using DynamicSpans = std::vector<DynamicSpan>;

/** A Symbol represents a string-token with a small index. */
using Symbol = uint32_t;

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
 * another object such as HeapStatData. StatNameStorage is not fully RAII --
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
class SymbolTable final {
public:
  /**
   * Efficient byte-encoded storage of an array of tokens. The most common
   * tokens are typically < 127, and are represented directly. tokens >= 128
   * spill into the next byte, allowing for tokens of arbitrary numeric value to
   * be stored. As long as the most common tokens are low-valued, the
   * representation is space-efficient. This scheme is similar to UTF-8. The
   * token ordering is dependent on the order in which stat-names are encoded
   * into the SymbolTable, which will not be optimal, but in practice appears
   * to be pretty good.
   *
   * This is exposed in the interface for the benefit of join(), which is
   * used in the hot-path to append two stat-names into a temp without taking
   * locks. This is used then in thread-local cache lookup, so that once warm,
   * no locks are taken when looking up stats.
   */
  using Storage = uint8_t[];
  using StoragePtr = std::unique_ptr<Storage>;

  /**
   * Intermediate representation for a stat-name. This helps store multiple
   * names in a single packed allocation. First we encode each desired name,
   * then sum their sizes for the single packed allocation. This is used to
   * store MetricImpl's tags and tagExtractedName.
   */
  class Encoding {
  public:
    /**
     * Before destructing SymbolEncoding, you must call moveToMemBlock. This
     * transfers ownership, and in particular, the responsibility to call
     * SymbolTable::clear() on all referenced symbols. If we ever wanted to be
     * able to destruct a SymbolEncoding without transferring it we could add a
     * clear(SymbolTable&) method.
     */
    ~Encoding();

    /**
     * Encodes a token into the vec.
     *
     * @param symbol the symbol to encode.
     */
    void addSymbols(const SymbolVec& symbols);

    /**
     * Decodes a uint8_t array into a SymbolVec.
     */
    static SymbolVec decodeSymbols(StatName stat_name);

    /**
     * Decodes a uint8_t array into a sequence of symbols and literal strings.
     * There are distinct lambdas for these two options. Calls to these lambdas
     * will be interleaved based on the sequencing of literal strings and
     * symbols held in the data.
     *
     * @param array the StatName encoded as a uint8_t array.
     * @param size the size of the array in bytes.
     * @param symbol_token_fn a function to be called whenever a symbol is encountered in the array.
     * @param string_view_token_fn a function to be called whenever a string literal is encountered.
     */
    static void decodeTokens(StatName stat_name, const std::function<void(Symbol)>& symbol_token_fn,
                             const std::function<void(absl::string_view)>& string_view_token_fn);

    /**
     * Returns the number of bytes required to represent StatName as a uint8_t
     * array, including the encoded size.
     */
    size_t bytesRequired() const {
      return data_bytes_required_ + encodingSizeBytes(data_bytes_required_);
    }

    /**
     * Moves the contents of the vector into an allocated array. The array
     * must have been allocated with bytesRequired() bytes.
     *
     * @param mem_block_builder memory block to receive the encoded bytes.
     */
    void moveToMemBlock(MemBlockBuilder<uint8_t>& mem_block_builder);

    /**
     * @param number A number to encode in a variable length byte-array.
     * @return The number of bytes it would take to encode the number.
     */
    static size_t encodingSizeBytes(uint64_t number);

    /**
     * @param num_data_bytes The number of bytes in a data-block.
     * @return The total number of bytes required for the data-block and its encoded size.
     */
    static size_t totalSizeBytes(size_t num_data_bytes) {
      return encodingSizeBytes(num_data_bytes) + num_data_bytes;
    }

    /**
     * Saves the specified number into the byte array, returning the next byte.
     * There is no guarantee that bytes will be aligned, so we can't cast to a
     * uint16_t* and assign, but must individually copy the bytes.
     *
     * Requires that the buffer be sized to accommodate encodingSizeBytes(number).
     *
     * @param number the number to write.
     * @param mem_block the memory into which to append the number.
     */
    static void appendEncoding(uint64_t number, MemBlockBuilder<uint8_t>& mem_block);

    /**
     * Appends stat_name's bytes into mem_block, which must have been allocated to
     * allow for stat_name.size() bytes.
     *
     * @param stat_name the stat_name to append.
     * @param mem_block the block of memory to append to.
     */
    static void appendToMemBlock(StatName stat_name, MemBlockBuilder<uint8_t>& mem_block);

    /**
     * Decodes a byte-array containing a variable-length number.
     *
     * @param The encoded byte array, written previously by appendEncoding.
     * @return A pair containing the decoded number, and the number of bytes consumed from encoding.
     */
    static std::pair<uint64_t, size_t> decodeNumber(const uint8_t* encoding);

  private:
    friend class StatName;
    friend class SymbolTable;
    class TokenIter;

    size_t data_bytes_required_{0};
    MemBlockBuilder<uint8_t> mem_block_;
  };

  SymbolTable();
  ~SymbolTable();

  /**
   * Decodes a vector of symbols back into its period-delimited stat name. If
   * decoding fails on any part of the symbol_vec, we release_assert and crash,
   * since this should never happen, and we don't want to continue running
   * with a corrupt stats set.
   *
   * @param stat_name the stat name.
   * @return std::string stringified stat_name.
   */
  std::string toString(const StatName& stat_name) const;

  /**
   * @return uint64_t the number of symbols in the symbol table.
   */
  uint64_t numSymbols() const;

  /**
   * Determines whether one StatName lexically precedes another. Note that
   * the lexical order may not exactly match the lexical order of the
   * elaborated strings. For example, stat-name of "-.-" would lexically
   * sort after "---" but when encoded as a StatName would come lexically
   * earlier. In practice this is unlikely to matter as those are not
   * reasonable names for Envoy stats.
   *
   * Note that this operation has to be performed with the context of the
   * SymbolTable so that the individual Symbol objects can be converted
   * into strings for lexical comparison.
   *
   * @param a the first stat name
   * @param b the second stat name
   * @return bool true if a lexically precedes b.
   */
  bool lessThan(const StatName& a, const StatName& b) const;

  /**
   * Joins two or more StatNames. For example if we have StatNames for {"a.b",
   * "c.d", "e.f"} then the joined stat-name matches "a.b.c.d.e.f". The
   * advantage of using this representation is that it avoids having to
   * decode/encode into the elaborated form, and does not require locking the
   * SymbolTable.
   *
   * Note that this method does not bump reference counts on the referenced
   * Symbols in the SymbolTable, so it's only valid as long for the lifetime of
   * the joined StatNames.
   *
   * This is intended for use doing cached name lookups of scoped stats, where
   * the scope prefix and the names to combine it with are already in StatName
   * form. Using this class, they can be combined without accessing the
   * SymbolTable or, in particular, taking its lock.
   *
   * @param stat_names the names to join.
   * @return Storage allocated for the joined name.
   */
  StoragePtr join(const StatNameVec& stat_names) const;

  /**
   * Populates a StatNameList from a list of encodings. This is not done at
   * construction time to enable StatNameList to be instantiated directly in
   * a class that doesn't have a live SymbolTable when it is constructed.
   *
   * @param names A pointer to the first name in an array, allocated by the caller.
   * @param num_names The number of names.
   * @param list The StatNameList representing the stat names.
   */
  void populateList(const StatName* names, uint32_t num_names, StatNameList& list);

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const;
#endif

  /**
   * Creates a StatNameSet.
   *
   * @param name the name of the set.
   * @return the set.
   */
  StatNameSetPtr makeSet(absl::string_view name);

  using RecentLookupsFn = std::function<void(absl::string_view, uint64_t)>;

  /**
   * Calls the provided function with the name of the most recently looked-up
   * symbols, including lookups on any StatNameSets, and with a count of
   * the recent lookups on that symbol.
   *
   * @param iter the function to call for every recent item.
   */
  uint64_t getRecentLookups(const RecentLookupsFn&) const;

  /**
   * Clears the recent-lookups structures.
   */
  void clearRecentLookups();

  /**
   * Sets the recent-lookup capacity.
   */
  void setRecentLookupCapacity(uint64_t capacity);

  /**
   * @return The configured recent-lookup tracking capacity.
   */
  uint64_t recentLookupCapacity() const;

  /**
   * Identifies the dynamic components of a stat_name into an array of integer
   * pairs, indicating the begin/end of spans of tokens in the stat-name that
   * are created from StatNameDynamicStore or StatNameDynamicPool.
   *
   * This can be used to reconstruct the same exact StatNames in
   * StatNames::mergeStats(), to enable stat continuity across hot-restart.
   *
   * @param stat_name the input stat name.
   * @return the array of pairs indicating the bounds.
   */
  DynamicSpans getDynamicSpans(StatName stat_name) const;

  bool lessThanLockHeld(const StatName& a, const StatName& b) const;

  template <class GetStatName, class Obj> struct StatNameCompare {
    StatNameCompare(const SymbolTable& symbol_table, GetStatName getter)
        : symbol_table_(symbol_table), getter_(getter) {}

    bool operator()(const Obj& a, const Obj& b) const;

    const SymbolTable& symbol_table_;
    GetStatName getter_;
  };

  /**
   * Sorts a range by StatName. This API is more efficient than
   * calling std::sort directly as it takes a single lock for the
   * entire sort, rather than locking on each comparison.
   *
   * @param begin the beginning of the range to sort
   * @param end the end of the range to sort
   * @param get_stat_name a functor that takes an Obj and returns a StatName.
   */
  template <class Obj, class Iter, class GetStatName>
  void sortByStatNames(Iter begin, Iter end, GetStatName get_stat_name) const {
    // Grab the lock once before sorting begins, so we don't have to re-take
    // it on every comparison.
    Thread::LockGuard lock(lock_);
    StatNameCompare<GetStatName, Obj> compare(*this, get_stat_name);
    std::sort(begin, end, compare);
  }

private:
  friend class StatName;
  friend class StatNameTest;
  friend class StatNameDeathTest;
  friend class StatNameDynamicStorage;
  friend class StatNameList;
  friend class StatNameStorage;

  /**
   * Encodes 'name' into the symbol table. Bumps reference counts for referenced
   * symbols. The caller must manage the storage, and is responsible for calling
   * SymbolTable::free() to release the reference counts.
   *
   * @param name The name to encode.
   * @return The encoded name, transferring ownership to the caller.
   *
   */
  StoragePtr encode(absl::string_view name);
  StoragePtr makeDynamicStorage(absl::string_view name);

  /**
   * Since SymbolTable does manual reference counting, a client of SymbolTable
   * must manually call free(symbol_vec) when it is freeing the backing store
   * for a StatName. This way, the symbol table will grow and shrink
   * dynamically, instead of being write-only.
   *
   * @param stat_name the stat name.
   */
  void free(const StatName& stat_name);

  /**
   * StatName backing-store can be managed by callers in a variety of ways
   * to minimize overhead. But any persistent reference to a StatName needs
   * to hold onto its own reference-counts for all symbols. This method
   * helps callers ensure the symbol-storage is maintained for the lifetime
   * of a reference.
   *
   * @param stat_name the stat name.
   */
  void incRefCount(const StatName& stat_name);

  struct SharedSymbol {
    SharedSymbol(Symbol symbol) : symbol_(symbol) {}

    Symbol symbol_;
    uint32_t ref_count_{1};
  };

  // This must be held during both encode() and free().
  mutable Thread::MutexBasicLockable lock_;

  /**
   * Decodes a uint8_t array into an array of period-delimited strings. Note
   * that some of the strings may have periods in them, in the case where
   * StatNameDynamicStorage was used.
   *
   * If decoding fails on any part of the encoding, we RELEASE_ASSERT and crash,
   * since this should never happen, and we don't want to continue running with
   * corrupt stat names.
   *
   * @param array the uint8_t array of encoded symbols and dynamic strings.
   * @param size the size of the array in bytes.
   * @return std::string the retrieved stat name.
   */
  std::vector<absl::string_view> decodeStrings(StatName stat_name) const;

  /**
   * Convenience function for encode(), symbolizing one string segment at a time.
   *
   * @param sv the individual string to be encoded as a symbol.
   * @return Symbol the encoded string.
   */
  Symbol toSymbol(absl::string_view sv) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  /**
   * Convenience function for decode(), decoding one symbol at a time.
   *
   * @param symbol the individual symbol to be decoded.
   * @return absl::string_view the decoded string.
   */
  absl::string_view fromSymbol(Symbol symbol) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

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
  Symbol next_symbol_ ABSL_GUARDED_BY(lock_);

  // If the free pool is exhausted, we monotonically increase this counter.
  Symbol monotonic_counter_;

  // Bitmap implementation.
  // The encode map stores both the symbol and the ref count of that symbol.
  // Using absl::string_view lets us only store the complete string once, in the decode map.
  using EncodeMap = absl::flat_hash_map<absl::string_view, SharedSymbol>;
  using DecodeMap = absl::flat_hash_map<Symbol, InlineStringPtr>;
  EncodeMap encode_map_ ABSL_GUARDED_BY(lock_);
  DecodeMap decode_map_ ABSL_GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ ABSL_GUARDED_BY(lock_);
  RecentLookups recent_lookups_ ABSL_GUARDED_BY(lock_);
};

// Base class for holding the backing-storing for a StatName. The two derived
// classes, StatNameStorage and StatNameDynamicStorage, share a need to hold an
// array of bytes, but use different representations.
class StatNameStorageBase {
public:
  StatNameStorageBase(SymbolTable::StoragePtr&& bytes) : bytes_(std::move(bytes)) {}
  StatNameStorageBase() = default;

  /**
   * @return a reference to the owned storage.
   */
  inline StatName statName() const;

  /**
   * @return the encoded data as a const pointer.
   */
  const uint8_t* bytes() const { return bytes_.get(); }

protected:
  void setBytes(SymbolTable::StoragePtr&& bytes) { bytes_ = std::move(bytes); }
  void clear() { bytes_.reset(); }

private:
  SymbolTable::StoragePtr bytes_;
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
class StatNameStorage : public StatNameStorageBase {
public:
  // Basic constructor for when you have a name as a string, and need to
  // generate symbols for it.
  StatNameStorage(absl::string_view name, SymbolTable& table);

  // Move constructor; needed for using StatNameStorage as an
  // absl::flat_hash_map value.
  StatNameStorage(StatNameStorage&& src) noexcept : StatNameStorageBase(std::move(src)) {}

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

    return H::combine(std::move(h), stat_name.dataAsStringView());
  }

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying representation.
   */
  uint64_t hash() const { return absl::Hash<StatName>()(*this); }

  bool operator==(const StatName& rhs) const {
    return dataAsStringView() == rhs.dataAsStringView();
  }
  bool operator!=(const StatName& rhs) const { return !(*this == rhs); }

  /**
   * @return size_t the number of bytes in the symbol array, excluding the
   *                overhead for the size itself.
   */
  size_t dataSize() const;

  /**
   * @return size_t the number of bytes in the symbol array, including the
   *                  overhead for the size itself.
   */
  size_t size() const { return SymbolTable::Encoding::totalSizeBytes(dataSize()); }

  /**
   * Copies the entire StatName representation into a MemBlockBuilder, including
   * the length metadata at the beginning. The MemBlockBuilder must not have
   * any other data in it.
   *
   * @param mem_block_builder the builder to receive the storage.
   */
  void copyToMemBlock(MemBlockBuilder<uint8_t>& mem_block_builder) {
    ASSERT(mem_block_builder.size() == 0);
    mem_block_builder.appendData(absl::MakeSpan(size_and_data_, size()));
  }

  /**
   * Appends the data portion of the StatName representation into a
   * MemBlockBuilder, excluding the length metadata. This is appropriate for
   * join(), where several stat-names are combined, and we only need the
   * aggregated length metadata.
   *
   * @param mem_block_builder the builder to receive the storage.
   */
  void appendDataToMemBlock(MemBlockBuilder<uint8_t>& storage) {
    storage.appendData(absl::MakeSpan(data(), dataSize()));
  }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

  /**
   * @return A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const {
    if (size_and_data_ == nullptr) {
      return nullptr;
    }
    return size_and_data_ + SymbolTable::Encoding::encodingSizeBytes(dataSize());
  }

  const uint8_t* dataIncludingSize() const { return size_and_data_; }

  /**
   * @return whether this is empty.
   */
  bool empty() const { return size_and_data_ == nullptr || dataSize() == 0; }

  /**
   * Determines whether this starts with the prefix. Note: dynamic segments
   * are not supported in the current implementation; this matching only works
   * for symbolic segments. However it OK for this to include dynamic segments
   * following the prefix.
   *
   * @param symbolic_prefix the prefix, which must not contain dynamic segments.
   */
  bool startsWith(StatName symbolic_prefix) const;

private:
  /**
   * Casts the raw data as a string_view. Note that this string_view will not
   * be in human-readable form, but it will be compatible with a string-view
   * hasher and comparator.
   */
  absl::string_view dataAsStringView() const {
    return {reinterpret_cast<const char*>(data()),
            static_cast<absl::string_view::size_type>(dataSize())};
  }

  const uint8_t* size_and_data_{nullptr};
};

StatName StatNameStorageBase::statName() const { return StatName(bytes_.get()); }

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
  StatNameManagedStorage(StatNameManagedStorage&& src) noexcept
      : StatNameStorage(std::move(src)), symbol_table_(src.symbol_table_) {}
  StatNameManagedStorage(StatName src, SymbolTable& table) noexcept
      : StatNameStorage(src, table), symbol_table_(table) {}

  ~StatNameManagedStorage() { free(symbol_table_); } // NOLINT(clang-analyzer-unix.Malloc)

private:
  SymbolTable& symbol_table_;
};

/**
 * Holds backing-store for a dynamic stat, where are no global locks needed
 * to create a StatName from a string, but there is no sharing of token data
 * between names, so there may be more memory consumed.
 */
class StatNameDynamicStorage : public StatNameStorageBase {
public:
  // Basic constructor based on a name. Note the table is used for a call to
  // encode the name, but no locks are taken in either implementation of the
  // SymbolTable api.
  StatNameDynamicStorage(absl::string_view name, SymbolTable& table)
      : StatNameStorageBase(table.makeDynamicStorage(name)) {}
  // Move constructor.
  StatNameDynamicStorage(StatNameDynamicStorage&& src) noexcept
      : StatNameStorageBase(std::move(src)) {}
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
 *   const uint8_t* storage = pool.addReturningStorage("name3");
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
   * Adds the StatName to the pool preserving the representation.
   * This avoids stringifying if we already have a StatName object
   * and is useful if parts of the StatName are dynamically encoded.
   * @param name the stat name to add the container.
   * @return the StatName held in the container for this name.
   *
   */
  StatName add(StatName name);

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
  const uint8_t* addReturningStorage(absl::string_view name);

private:
  // We keep the stat names in a vector of StatNameStorage, storing the
  // SymbolTable reference separately. This saves 8 bytes per StatName,
  // at the cost of having a destructor that calls clear().
  SymbolTable& symbol_table_;
  std::vector<StatNameStorage> storage_vector_;
};

/**
 * Maintains storage for a collection of StatName objects constructed from
 * dynamically discovered strings. Like StatNameDynamicStorage, this has an RAII
 * usage model. Creating StatNames with this interface do not incur a
 * SymbolTable lock, but tokens are not shared across StatNames.
 *
 * The SymbolTable is required as a constructor argument to assist in encoding
 * the stat-names.
 *
 * Example usage:
 *   StatNameDynamicPool pool(symbol_table);
 *   StatName name1 = pool.add("name1");
 *   StatName name2 = pool.add("name2");
 *
 * Note; StatNameDynamicPool::add("foo") != StatNamePool::add("foo"), even
 * though their string representations are identical. They also will not match
 * in map lookups. Tests for StatName with dynamic components must therefore
 * be looked up by string, via Stats::TestUtil::TestStore.
 */
class StatNameDynamicPool {
public:
  explicit StatNameDynamicPool(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}

  /**
   * @param name the name to add the container.
   * @return the StatName held in the container for this name.
   */
  StatName add(absl::string_view name);

private:
  // We keep the stat names in a vector of StatNameStorage, storing the
  // SymbolTable reference separately. This saves 8 bytes per StatName,
  // at the cost of having a destructor that calls clear().
  SymbolTable& symbol_table_;
  std::vector<StatNameDynamicStorage> storage_vector_;
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
  friend class SymbolTable;

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
   * For SymbolTable, each symbol is 1 or more bytes, in a variable-length
   * encoding. See SymbolTable::Encoding::addSymbol for details.
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
   * @return the number of elements in the set.
   */
  size_t size() const { return hash_set_.size(); }

private:
  HashSet hash_set_;
};

// Captures StatNames for lookup by string, keeping a map of 'built-ins' that is
// expected to be populated during initialization.
//
// Ideally, builtins should be added during process initialization, in the
// outermost relevant context. And as the builtins map is not mutex protected,
// builtins must *not* be added to an existing StatNameSet in the request-path.
//
// It is fine to populate a new StatNameSet when (for example) an xDS
// message reveals a new set of names to be used as stats. The population must
// be completed prior to exposing the new StatNameSet to worker threads.
//
// To create stats using names discovered in the request path, dynamic stat
// names must be used (see StatNameDynamicStorage). Consider using helper
// methods such as Stats::Utility::counterFromElements in common/stats/utility.h
// to simplify the process of allocating and combining stat names and creating
// counters, gauges, and histograms from them.
class StatNameSet {
public:
  // This object must be instantiated via SymbolTable::makeSet(), thus constructor is private.

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
   * Finds a builtin StatName by name. If the builtin has not been registered,
   * then the fallback is returned.
   *
   * @return the StatName or fallback.
   */
  StatName getBuiltin(absl::string_view token, StatName fallback) const;

  /**
   * Adds a StatName using the pool, but without remembering it in any maps.
   *
   * For convenience, StatNameSet offers pass-through thread-safe access to
   * its mutex-protected pool. This is useful in constructor initializers, when
   * StatNames are needed both from compile-time constants, as well as from
   * other constructor args, e.g.
   *    MyClass(const std::vector<absl::string_view>& strings, Stats::SymbolTable& symbol_table)
   *        : stat_name_set_(symbol_table),
   *          known_const_(stat_name_set_.add("known_const")) { // unmapped constants from pool
   *      stat_name_set_.rememberBuiltins(strings); // mapped builtins.
   *    }
   * This avoids the need to make two different pools; one backing the
   * StatNameSet mapped entries, and the other backing the set passed in via the
   * constructor.
   *
   * @param str The string to add as a StatName
   * @return The StatName for str.
   */
  StatName add(absl::string_view str) {
    absl::MutexLock lock(&mutex_);
    return pool_.add(str);
  }

private:
  friend class SymbolTable;

  StatNameSet(SymbolTable& symbol_table, absl::string_view name);

  const std::string name_;
  Stats::StatNamePool pool_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  using StringStatNameMap = absl::flat_hash_map<std::string, Stats::StatName>;
  StringStatNameMap builtin_stat_names_;
};

template <class GetStatName, class Obj>
bool SymbolTable::StatNameCompare<GetStatName, Obj>::operator()(const Obj& a, const Obj& b) const {
  StatName a_stat_name = getter_(a);
  StatName b_stat_name = getter_(b);
  return symbol_table_.lessThanLockHeld(a_stat_name, b_stat_name);
}

using SymbolTablePtr = std::unique_ptr<SymbolTable>;

// TODO(jmarantz): rename all remaining ~47 occurrences of SymbolTableImpl in
// the codebase to SymbolTable, and drop this alias.
using SymbolTableImpl = SymbolTable;

} // namespace Stats
} // namespace Envoy
