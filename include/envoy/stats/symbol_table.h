#pragma once

#include "common/common/hash.h"

namespace Envoy {
namespace Stats {

/** Efficient byte-encoded storage an array of tokens, which are typically < 127 */
using SymbolStorage = uint8_t[];

// Interface for managing symbol tables.
class SymbolTable;

/**
 * Efficiently represents a stat name using a variable-length array of uint8_t.
 * This class does not own the backing store for this array; the backing-store
 * can be held in StatNameStorage, or it can be packed more tightly into another
 * object.
 *
 * For large numbers of clusters, there are a huge number of StatNames so
 * avoiding extra per-stat pointers has a significant memory impact.
 */
class StatName {
public:
  // Constructs a StatName object directly referencing the storage of another
  // StatName.
  explicit StatName(const SymbolStorage symbol_array) : symbol_array_(symbol_array) {}

  // Constructs an empty StatName object.
  StatName() : symbol_array_(nullptr) {}

  // Constructs a StatName object with new storage, which must be of size
  // src.numBytesIncludingLenggth(). This is used in the a flow where we first
  // construct a StatName for lookup in a cache, and then on a miss need/ to
  // store the data directly.
  StatName(const StatName& src, SymbolStorage memory);

  std::string toString(const SymbolTable& table) const;

  /**
   * Note that this hash function will return a different hash than that of
   * the elaborated string.
   *
   * @return uint64_t a hash of the underlying representation.
   */
  uint64_t hash() const {
    const char* cdata = reinterpret_cast<const char*>(data());
    return HashUtil::xxHash64(absl::string_view(cdata, numBytes()));
  }

  bool operator==(const StatName& rhs) const {
    const uint64_t sz = numBytes();
    return sz == rhs.numBytes() && memcmp(data(), rhs.data(), sz * sizeof(uint8_t)) == 0;
  }
  bool operator!=(const StatName& rhs) const { return !(*this == rhs); }

  /**
   * @return uint64_t the number of bytes in the symbol array, excluding the two-byte
   *                  overhead for the size itself.
   */
  uint64_t numBytes() const {
    return symbol_array_[0] | (static_cast<uint64_t>(symbol_array_[1]) << 8);
  }

  const uint8_t* symbolArray() const { return symbol_array_; }

  /**
   * @return uint64_t the number of bytes in the symbol array, including the two-byte
   *                  overhead for the size itself.
   */
  uint64_t numBytesIncludingLength() const { return numBytes() + 2; }

  void copyToStorage(SymbolStorage storage) {
    memcpy(storage, symbol_array_, numBytesIncludingLength());
  }

protected:
  friend SymbolTable;
  friend class StatNameTest;
  friend class StatNameJoiner;
  friend class StatNameStorage;

  /**
   * @return uint8_t* A pointer to the first byte of data (skipping over size bytes).
   */
  const uint8_t* data() const {
    return symbol_array_ + 2;
  }

  const uint8_t* symbol_array_;
};

} // namespace Stats
} // namespace Envoy
