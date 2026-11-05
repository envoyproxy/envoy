#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Config {

class XdsSymbolTable;
using XdsSymbolTableSharedPtr = std::shared_ptr<XdsSymbolTable>;

using XdsSymbol = uint32_t;

class XdsResourceNameStorage;

/**
 * An immutable, lightweight value-type view of an encoded xDS resource name.
 * Encoded as a contiguous sequence of variable-length (`LEB128`) symbol IDs.
 *
 * NOTE ON LIFETIME & COMPARISON:
 * 1. Scope of Validity: An XdsResourceName is only valid for comparison (via
 *    operator==, operator!=, operator<, and AbslHashValue) with other
 *    XdsResourceName instances originating from the SAME XdsSymbolTable. Because
 *    symbol IDs are assigned on-demand within each table, comparing names
 *    across different symbol tables is invalid and produces undefined results.
 * 2. Non-Alphabetical Ordering: operator< provides a strict weak ordering based
 *    on byte-wise comparison of the encoded `LEB128` symbol sequence. It does NOT
 *    correspond to alphabetical (lexicographical ASCII) order of the decoded
 *    strings. This design enables fast, lock-free, zero-allocation comparisons
 *    for sorted containers (such as absl::btree_map in ClusterManager).
 *    If alphabetical ordering is required for display or export, materialize
 *    the strings via XdsSymbolTable::toString().
 */
class XdsResourceName {
public:
  constexpr XdsResourceName() = default;

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0 || data_ == nullptr; }
  absl::Span<const uint8_t> span() const {
    if (data_ == nullptr || size_ == 0) {
      return {};
    }
    return {data_, size_};
  }

  /**
   * Compares two XdsResourceName instances for equality.
   * Both instances MUST originate from the same XdsSymbolTable.
   */
  bool operator==(const XdsResourceName& other) const {
    if (size_ != other.size_) {
      return false;
    }
    if (data_ == other.data_ || size_ == 0) {
      return true;
    }
    return std::memcmp(data_, other.data_, size_) == 0;
  }

  bool operator!=(const XdsResourceName& other) const { return !(*this == other); }

  /**
   * Provides a strict weak ordering for sorted associative containers (std::set,
   * absl::btree_map).
   *
   * Note: This is an efficient byte-wise comparison of the encoded symbol IDs,
   * NOT an alphabetical ASCII string comparison.
   */
  bool operator<(const XdsResourceName& other) const {
    if (data_ == other.data_) {
      return false;
    }
    const size_t min_len = std::min(size_, other.size_);
    if (min_len > 0) {
      const int cmp = std::memcmp(data_, other.data_, min_len);
      if (cmp != 0) {
        return cmp < 0;
      }
    }
    return size_ < other.size_;
  }

  template <typename H> friend H AbslHashValue(H h, const XdsResourceName& name) {
    if (name.size_ == 0 || name.data_ == nullptr) {
      return H::combine(std::move(h), 0);
    }
    return H::combine_contiguous(std::move(h), name.data_, name.size_);
  }

private:
  friend class XdsResourceNameStorage;
  friend class XdsSymbolTable;

  constexpr XdsResourceName(const uint8_t* data, size_t size)
      : data_(data), size_(data == nullptr ? 0 : size) {}

  const uint8_t* data_{nullptr};
  size_t size_{0};
};

/**
 * RAII owning storage for an encoded xDS resource name.
 * Manages symbol ref-counts in the backing XdsSymbolTable.
 */
class XdsResourceNameStorage {
public:
  XdsResourceNameStorage(const XdsSymbolTableSharedPtr& table, absl::string_view name);
  ~XdsResourceNameStorage();

  XdsResourceNameStorage(XdsResourceNameStorage&& other) noexcept;
  XdsResourceNameStorage& operator=(XdsResourceNameStorage&& other) noexcept;

  XdsResourceNameStorage(const XdsResourceNameStorage& other);
  XdsResourceNameStorage& operator=(const XdsResourceNameStorage& other);

  XdsResourceName resourceName() const { return {data_.get(), size_}; }

  size_t size() const { return size_; }

  bool operator==(const XdsResourceNameStorage& other) const {
    return resourceName() == other.resourceName();
  }

  bool operator!=(const XdsResourceNameStorage& other) const { return !(*this == other); }

  template <typename H> friend H AbslHashValue(H h, const XdsResourceNameStorage& storage) {
    return AbslHashValue(std::move(h), storage.resourceName());
  }

private:
  friend class XdsSymbolTable;

  XdsResourceNameStorage(XdsSymbolTableSharedPtr table, std::unique_ptr<uint8_t[]> data,
                         uint32_t size);

  XdsSymbolTableSharedPtr table_;
  std::unique_ptr<uint8_t[]> data_;
  uint32_t size_{0};
};

/**
 * Transparent hash functor allowing heterogeneous lookup in Abseil hash containers
 * between `XdsResourceNameStorage` (owning) and `XdsResourceName` (non-owning view).
 */
struct XdsResourceNameHash {
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  size_t operator()(const XdsResourceName& name) const { return absl::HashOf(name); }
  size_t operator()(const XdsResourceNameStorage& storage) const {
    return absl::HashOf(storage.resourceName());
  }
};

/**
 * Transparent equality functor allowing heterogeneous lookup in Abseil hash containers
 * between `XdsResourceNameStorage` (owning) and `XdsResourceName` (non-owning view).
 */
struct XdsResourceNameEq {
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  bool operator()(const XdsResourceName& lhs, const XdsResourceName& rhs) const {
    return lhs == rhs;
  }
  bool operator()(const XdsResourceNameStorage& lhs, const XdsResourceNameStorage& rhs) const {
    return lhs.resourceName() == rhs.resourceName();
  }
  bool operator()(const XdsResourceNameStorage& lhs, const XdsResourceName& rhs) const {
    return lhs.resourceName() == rhs;
  }
  bool operator()(const XdsResourceName& lhs, const XdsResourceNameStorage& rhs) const {
    return lhs == rhs.resourceName();
  }
};

/**
 * XdsSymbolTable interns sub-tokens and delimiters of xDS resource names.
 * Thread-safe: mutations and reads are guarded by Thread::MutexBasicLockable.
 */
class XdsSymbolTable : public std::enable_shared_from_this<XdsSymbolTable> {
public:
  static XdsSymbolTableSharedPtr create();
  ~XdsSymbolTable() = default;

  // Encodes a resource name into owning RAII storage.
  XdsResourceNameStorage encode(absl::string_view name);

  // Attempts to encode `name` using only symbols already active in the table, without mutating
  // symbol state or ref-counts. Writes the encoded `LEB128` bytes into `scratch` (which must
  // outlive the returned `XdsResourceName` view) and returns the view if all tokens exist, or
  // `std::nullopt` if any token is unknown.
  std::optional<XdsResourceName> tryEncode(absl::string_view name,
                                           absl::InlinedVector<uint8_t, 64>& scratch) const;

  // Decodes an encoded XdsResourceName back into its original string with 100% fidelity.
  std::string toString(const XdsResourceName& name) const;

  // Decodes from a list of symbol IDs.
  std::string toString(absl::Span<const XdsSymbol> symbols) const;

  // Number of unique symbols allocated in the table.
  size_t numSymbols() const;

  // Number of currently active (referenced) symbols.
  size_t numActiveSymbols() const;

  // Decodes symbols from encoded bytes using an inline visitor callback.
  // Avoids heap allocating a temporary vector. Returns false if the `LEB128`
  // byte stream is malformed.
  template <typename Callback>
  static bool forEachSymbol(absl::Span<const uint8_t> bytes, Callback&& callback) {
    size_t i = 0;
    while (i < bytes.size()) {
      XdsSymbol sym = 0;
      int shift = 0;
      bool terminated = false;
      while (i < bytes.size()) {
        uint8_t b = bytes[i++];
        if (shift > 28 || (shift == 28 && (b & 0x70) != 0)) {
          // Shift overflow or 32-bit integer overflow: malformed `LEB128` input.
          return false;
        }
        sym |= static_cast<XdsSymbol>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
          terminated = true;
          break;
        }
        shift += 7;
      }
      if (!terminated) {
        // Incomplete/truncated `LEB128` sequence at EOF: malformed input.
        return false;
      }
      callback(sym);
    }
    return true;
  }

  // Increment/decrement symbol ref counts directly from encoded bytes without vector allocation.
  void addRef(absl::Span<const uint8_t> bytes);
  void releaseRef(absl::Span<const uint8_t> bytes);

  // Increment/decrement symbol ref counts.
  void addRef(absl::Span<const XdsSymbol> symbols);
  void releaseRef(absl::Span<const XdsSymbol> symbols);

  // Decodes symbols from encoded bytes into a vector.
  static std::vector<XdsSymbol> decodeBytesToSymbols(absl::Span<const uint8_t> bytes);

  // Well-known reserved symbols for common single delimiters.
  static constexpr XdsSymbol Slash = 1;      // '/'
  static constexpr XdsSymbol Dot = 2;        // '.'
  static constexpr XdsSymbol Colon = 3;      // ':'
  static constexpr XdsSymbol Question = 4;   // '?'
  static constexpr XdsSymbol Ampersand = 5;  // '&'
  static constexpr XdsSymbol Equals = 6;     // '='
  static constexpr XdsSymbol Underscore = 7; // '_'
  static constexpr XdsSymbol Dash = 8;       // '-'
  static constexpr XdsSymbol SchemeSep = 9;  // '://'
  static constexpr XdsSymbol FirstDynamicSymbol = 10;

private:
  friend class XdsResourceNameStorage;

  XdsSymbolTable();

  struct TokenItem {
    XdsSymbol delimiter_sym{0};
    absl::string_view word;
  };

  // Tokenizes name into tokens and delimiters outside the lock.
  static absl::InlinedVector<TokenItem, 32> splitTokens(absl::string_view name);

  // Encodes a sequence of symbols into a compact byte vector using `LEB128`.
  static std::vector<uint8_t> encodeSymbolsToBytes(absl::Span<const XdsSymbol> symbols);

  XdsSymbol toSymbol(absl::string_view token) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void addRefLocked(absl::Span<const XdsSymbol> symbols) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  mutable Thread::MutexBasicLockable lock_;
  absl::flat_hash_map<absl::string_view, XdsSymbol> encode_map_ ABSL_GUARDED_BY(lock_);
  std::deque<std::string> decode_table_ ABSL_GUARDED_BY(lock_);
  std::vector<uint32_t> ref_counts_ ABSL_GUARDED_BY(lock_);
  std::vector<XdsSymbol> free_list_ ABSL_GUARDED_BY(lock_);
};

} // namespace Config
} // namespace Envoy
