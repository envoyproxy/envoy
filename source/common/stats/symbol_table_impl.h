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
#include "common/common/thread.h"
#include "common/common/utility.h"

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
 * Represents an 8-bit encoding of a vector of symbols, used as a transient
 * representation during encoding and prior to retained allocation.
 */
class SymbolEncoding {
public:
  /**
   * Before destructing SymbolEncoding, you must call moveToStorage. This
   * transfers ownership, and in particular, the responsibility to call
   * SymbolTable::clear() on all referenced symbols. If we ever wanted
   * to be able to destruct a SymbolEncoding without transferring it
   * we could add a clear(SymbolTable&) method.
   */
  ~SymbolEncoding();

  /**
   * Encodes a token into the vec.
   *
   * @param symbol the symbol to encode.
   */
  void addSymbol(Symbol symbol);

  /**
   * Encodes an entire string into the vec, on behalf of FakeSymbolTableImpl.
   * TODO(jmarantz): delete this method when FakeSymbolTableImpl is deleted.
   *
   * @param str The string to encode.
   */
  void addStringForFakeSymbolTable(absl::string_view str);

  /**
   * Decodes a uint8_t array into a SymbolVec.
   */
  static SymbolVec decodeSymbols(const SymbolTable::Storage array, uint64_t size);

  /**
   * Returns the number of bytes required to represent StatName as a uint8_t
   * array, including the encoded size.
   */
  uint64_t bytesRequired() const { return size() + StatNameSizeEncodingBytes; }

  /**
   * Returns the number of uint8_t entries we collected while adding symbols.
   */
  uint64_t size() const { return vec_.size(); }

  /**
   * Moves the contents of the vector into an allocated array. The array
   * must have been allocated with bytesRequired() bytes.
   *
   * @param array destination memory to receive the encoded bytes.
   * @return uint64_t the number of bytes transferred.
   */
  uint64_t moveToStorage(SymbolTable::Storage array);

  void swap(SymbolEncoding& src) { vec_.swap(src.vec_); }

private:
  std::vector<uint8_t> vec_;
};

/**
 * SymbolTableImpl manages a namespace optimized for stats, which are typically
 * composed of arrays of "."-separated tokens, with a significant overlap
 * between the tokens. Each token is mapped to a Symbol (uint32_t) and
 * reference-counted so that no-longer-used symbols can be reclaimed.
 *
 * We use a uint8_t array to encode a "."-deliminated stat-name into arrays of
 * integer symbol symbol IDs in order to conserve space, as in practice the
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
  SymbolTableImpl();
  ~SymbolTableImpl() override;

  // SymbolTable
  std::string toString(const StatName& stat_name) const override;
  SymbolEncoding encode(absl::string_view name) override;
  uint64_t numSymbols() const override;
  bool lessThan(const StatName& a, const StatName& b) const override;
  void free(const StatName& stat_name) override;
  void incRefCount(const StatName& stat_name) override;
  SymbolTable::StoragePtr join(const std::vector<StatName>& stat_names) const override;

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint() const override;
#endif

private:
  friend class StatName;
  friend class StatNameTest;

  struct SharedSymbol {
    SharedSymbol(Symbol symbol) : symbol_(symbol), ref_count_(1) {}

    Symbol symbol_;
    uint32_t ref_count_;
  };

  // This must be called during both encode() and free().
  mutable Thread::MutexBasicLockable lock_;

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

  // Stages a new symbol for use. To be called after a successful insertion.
  void newSymbol();

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
  using EncodeMap = absl::flat_hash_map<absl::string_view, SharedSymbol, StringViewHash>;
  using DecodeMap = absl::flat_hash_map<Symbol, std::unique_ptr<std::string>>;
  EncodeMap encode_map_ GUARDED_BY(lock_);
  DecodeMap decode_map_ GUARDED_BY(lock_);

  // Free pool of symbols for re-use.
  // TODO(ambuc): There might be an optimization here relating to storing ranges of freed symbols
  // using an Envoy::IntervalSet.
  std::stack<Symbol> pool_ GUARDED_BY(lock_);
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
 * a StatName from a string. Instead it should be used via StatNameTempStorage.
 */
class StatNameStorage {
public:
  // Basic constructor for when you have a name as a string, and need to
  // generate symbols for it.
  StatNameStorage(absl::string_view name, SymbolTable& table);

  // Move constructor; needed for using StatNameStorage as an
  // absl::flat_hash_map value.
  StatNameStorage(StatNameStorage&& src) : bytes_(std::move(src.bytes_)) {}

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
  StatName() : size_and_data_(nullptr) {}

  // Constructs a StatName object with new storage, which must be of size
  // src.size(). This is used in the a flow where we first construct a StatName
  // for lookup in a cache, and then on a miss need to store the data directly.
  StatName(const StatName& src, SymbolTable::Storage memory);

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying representation.
   */
  uint64_t hash() const {
    const char* cdata = reinterpret_cast<const char*>(data());
    return HashUtil::xxHash64(absl::string_view(cdata, dataSize()));
  }

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
   * @return uint8_t* A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const { return size_and_data_ + StatNameSizeEncodingBytes; }

private:
  const uint8_t* size_and_data_;
};

StatName StatNameStorage::statName() const { return StatName(bytes_.get()); }

/**
 * Contains the backing store for a StatName and enough context so it can
 * self-delete through RAII. This works by augmenting StatNameStorage with a
 * reference to the SymbolTable&, so it has an extra 8 bytes of footprint. It
 * is intended to be used in tests or as a scoped temp in a function, rather
 * than stored in a larger structure such as a map, where the redundant copies
 * of the SymbolTable& would be costly in aggregate.
 */
class StatNameTempStorage : public StatNameStorage {
public:
  // Basic constructor for when you have a name as a string, and need to
  // generate symbols for it.
  StatNameTempStorage(absl::string_view name, SymbolTable& table)
      : StatNameStorage(name, table), symbol_table_(table) {}

  // Obtains new backing storage for an already existing StatName.
  StatNameTempStorage(StatName src, SymbolTable& table)
      : StatNameStorage(src, table), symbol_table_(table) {}

  ~StatNameTempStorage() { free(symbol_table_); }

private:
  SymbolTable& symbol_table_;
};

// Represents an ordered container of StatNames. The encoding for each StatName
// is byte-packed together, so this carries less overhead than allocating the
// storage separately. The tradeoff is there is no random access; you can only
// iterate through the StatNames.
//
// The maximum size of the list is 255 elements, so the length can fit in a
// byte. It would not be difficult to increase this, but there does not appear
// to be a current need.
class StatNameList {
public:
  ~StatNameList();

  /**
   * Populates the StatNameList from a list of encodings. This is not done at
   * construction time to enable StatNameList to be instantiated directly in
   * a class that doesn't have a live SymbolTable when it is constructed.
   *
   * @param encodings The list names to encode.
   * @param symbol_table The symbol table in which to encode the names.
   */
  void populate(const std::vector<absl::string_view>& encodings, SymbolTable& symbol_table);

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
  std::unique_ptr<uint8_t[]> storage_;
};

// Helper class for constructing hash-tables with StatName keys.
struct StatNameHash {
  size_t operator()(const StatName& a) const { return a.hash(); }
};

// Helper class for constructing hash-tables with StatName keys.
struct StatNameCompare {
  bool operator()(const StatName& a, const StatName& b) const { return a == b; }
};

// Value-templatized hash-map with StatName key.
template <class T>
using StatNameHashMap = absl::flat_hash_map<StatName, T, StatNameHash, StatNameCompare>;

// Hash-set of StatNames
using StatNameHashSet = absl::flat_hash_set<StatName, StatNameHash, StatNameCompare>;

// Helper class for sorting StatNames.
struct StatNameLessThan {
  StatNameLessThan(const SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  bool operator()(const StatName& a, const StatName& b) const {
    return symbol_table_.lessThan(a, b);
  }

  const SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
