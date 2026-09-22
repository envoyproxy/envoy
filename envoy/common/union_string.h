#pragma once

#include <algorithm>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {

// This includes the NULL (StringUtil::itoa technically only needs 21).
inline constexpr size_t MaxIntegerLength{32};

inline void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
}

/**
 * Storage for UnionStringBase. Holds either a reference to data owned elsewhere, or data owned
 * by this object, which lives inline for short strings and on the heap otherwise.
 *
 * The three states are distinguished by capacity_ alone, so no separate discriminator is needed:
 *   capacity_ == 0                    Reference: the data lives at ref_.
 *   capacity_ == InlineStringCapacity Inline: the data lives in inline_.
 *   capacity_ >  InlineStringCapacity Heap: the data lives at heap_.
 * A heap buffer is never allocated with a capacity <= InlineStringCapacity, which keeps the
 * encoding unambiguous; computeCapacity() asserts that invariant.
 */
class UnionStringStorage {
public:
  enum class StorageLocation { Reference, Inline, Heap };
  /**
   * Number of bytes of string data held by UnionStringStorage without a heap allocation.
   */
  constexpr static uint32_t kInlineStringCapacity{128};
  UnionStringStorage() = default;

  explicit UnionStringStorage(absl::string_view ref_value)
      : size_(static_cast<uint32_t>(ref_value.size())), capacity_(0), ref_(ref_value.data()) {
    ASSERT(ref_value.size() <= std::numeric_limits<uint32_t>::max());
  }

  UnionStringStorage(UnionStringStorage&& move_value) noexcept
      : size_(move_value.size_), capacity_(move_value.capacity_) {
    adoptData(move_value);
  }

  UnionStringStorage& operator=(UnionStringStorage&& move_value) noexcept {
    if (&move_value != this) {
      freeIfHeap();
      size_ = move_value.size_;
      capacity_ = move_value.capacity_;
      adoptData(move_value);
    }
    return *this;
  }

  UnionStringStorage(const UnionStringStorage&) = delete;
  UnionStringStorage& operator=(const UnionStringStorage&) = delete;

  ~UnionStringStorage() { freeIfHeap(); }

  StorageLocation storageLocation() const {
    switch (capacity_) {
    case 0:
      return StorageLocation::Reference;
    case kInlineStringCapacity:
      return StorageLocation::Inline;
    default:
      return StorageLocation::Heap;
    }
  }
  uint32_t size() const { return size_; }

  const char* data() const {
    switch (storageLocation()) {
    case StorageLocation::Reference:
      return ref_;
    case StorageLocation::Heap:
      return heap_;
    case StorageLocation::Inline:
      return inline_;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  char* mutableData() {
    const StorageLocation location = storageLocation();
    switch (location) {
    case StorageLocation::Reference:
      ASSERT(location != StorageLocation::Reference);
      return nullptr;
    case StorageLocation::Heap:
      return heap_;
    case StorageLocation::Inline:
      return inline_;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  /**
   * Point at data owned elsewhere, releasing any owned data.
   */
  void setReference(absl::string_view ref_value) {
    ASSERT(ref_value.size() <= std::numeric_limits<uint32_t>::max());
    freeIfHeap();
    size_ = static_cast<uint32_t>(ref_value.size());
    capacity_ = 0;
    ref_ = ref_value.data();
  }

  /**
   * Take ownership of a copy of data, discarding any data held previously.
   */
  void assign(const char* data, uint32_t size) {
    reserveDiscard(size);
    if (size != 0) {
      memmove(mutableData(), data, size); // NOLINT(safe-memcpy)
    }
    size_ = size;
  }

  /**
   * Take ownership of a copy of data appended to the data held currently.
   */
  void append(const char* data, uint32_t size) {
    if (size == 0) {
      return;
    }
    const uint64_t new_size = static_cast<uint64_t>(size_) + size;
    ASSERT(new_size <= std::numeric_limits<uint32_t>::max());
    reservePreserve(static_cast<uint32_t>(new_size));
    memmove(mutableData() + size_, data, size); // NOLINT(safe-memcpy)
    size_ = static_cast<uint32_t>(new_size);
  }

  /**
   * Discard owned data, releasing any heap allocation. References are left untouched.
   */
  void clear() {
    if (storageLocation() == StorageLocation::Reference) {
      return;
    }
    freeIfHeap();
    size_ = 0;
  }

  /**
   * Shrink owned data, which must already be at least new_size bytes long.
   */
  void shrink(uint32_t new_size) {
    ASSERT(storageLocation() != StorageLocation::Reference && new_size <= size_);
    size_ = new_size;
  }

private:
  void freeIfHeap() {
    switch (storageLocation()) {
    case StorageLocation::Heap:
      delete[] heap_;
      capacity_ = kInlineStringCapacity;
      break;
    case StorageLocation::Inline:
    case StorageLocation::Reference:
      break;
    }
  }

  // Takes over move_value's data, leaving it holding no heap allocation.
  void adoptData(UnionStringStorage& move_value) noexcept {
    switch (storageLocation()) {
    case StorageLocation::Reference:
      ref_ = move_value.ref_;
      break;
    case StorageLocation::Heap:
      heap_ = move_value.heap_;
      move_value.size_ = 0;
      move_value.capacity_ = kInlineStringCapacity;
      break;
    case StorageLocation::Inline:
      memcpy(inline_, move_value.inline_, move_value.size_); // NOLINT(safe-memcpy)
      break;
    }
  }

  // Computes the new heap buffer capacity to hold the required bytes. Grows geometrically to keep
  // repeated appends linear, and never returns a capacity the Inline state could be confused with.
  static uint32_t computeCapacity(uint32_t required, uint32_t current_capacity) {
    ASSERT(required > kInlineStringCapacity);
    const uint64_t doubled = 2 * static_cast<uint64_t>(current_capacity);
    const uint64_t capacity = std::max<uint64_t>(required, doubled);
    return static_cast<uint32_t>(
        std::min<uint64_t>(capacity, std::numeric_limits<uint32_t>::max()));
  }

  // Own at least required bytes of capacity. The data held currently is discarded.
  void reserveDiscard(uint32_t required) {
    if (storageLocation() != StorageLocation::Reference && capacity_ >= required) {
      return;
    }
    if (required <= kInlineStringCapacity) {
      freeIfHeap();
      capacity_ = kInlineStringCapacity;
      return;
    }
    const uint32_t new_capacity = computeCapacity(required, capacity_);
    char* new_heap = new char[new_capacity];
    freeIfHeap();
    heap_ = new_heap;
    capacity_ = new_capacity;
  }

  // Own at least required bytes of capacity, preserving the size_ bytes held currently.
  void reservePreserve(uint32_t required) {
    const StorageLocation location = storageLocation();
    if (location != StorageLocation::Reference && capacity_ >= required) {
      return;
    }
    if (required <= kInlineStringCapacity) {
      // Only reachable from the Reference state, which holds no inline data yet.
      ASSERT(location == StorageLocation::Reference);
      memcpy(inline_, ref_, size_); // NOLINT(safe-memcpy)
      capacity_ = kInlineStringCapacity;
      return;
    }
    const uint32_t new_capacity = computeCapacity(required, capacity_);
    char* new_heap = new char[new_capacity];
    memcpy(new_heap, data(), size_); // NOLINT(safe-memcpy)
    freeIfHeap();
    heap_ = new_heap;
    capacity_ = new_capacity;
  }

  uint32_t size_{0};
  uint32_t capacity_{kInlineStringCapacity}; // Default to empty inline storage.
  union {
    const char* ref_;
    char* heap_;
    char inline_[kInlineStringCapacity];
  };
};

static_assert(UnionStringStorage::kInlineStringCapacity > MaxIntegerLength,
              "setInteger() writes into the inline buffer without allocating.");

static_assert(sizeof(UnionStringStorage) == UnionStringStorage::kInlineStringCapacity + 8,
              "UnionStringStorage should hold no state beyond its size, capacity and data.");

/**
 * This is a string implementation that unified string reference and owned string. It is heavily
 * optimized for performance. It supports 2 different types of storage and can switch between them:
 * 1) A string reference.
 * 2) Owned data, held inline for short strings and on the heap if needed.
 */
template <class Validator> class UnionStringBase {
public:
  using Storage = UnionStringStorage;

  /**
   * Default constructor. Sets up for inline storage.
   */
  UnionStringBase() { assertValid(); }

  inline void assertValid() const {
    ASSERT(valid(), absl::StrCat(typeid(Validator).name(), " failed to validate string \"",
                                 getStringView(), "\""));
  }

  /**
   * Constructor for a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  explicit UnionStringBase(absl::string_view ref_value) : buffer_(ref_value) { assertValid(); }

  UnionStringBase(UnionStringBase&& move_value) noexcept : buffer_(std::move(move_value.buffer_)) {
    move_value.clear();
    // Move constructor does not validate and relies on the source object validating its mutations.
  }
  ~UnionStringBase() = default;

  /**
   * Append data to an existing string. If the string is a reference string the reference data is
   * not copied.
   */
  void append(const char* data, uint32_t data_size) {
    // Make sure the requested memory allocation is below uint32_t::max
    validateCapacity(static_cast<uint64_t>(data_size) + size());
    ASSERT(valid(absl::string_view(data, data_size)),
           absl::StrCat(typeid(Validator).name(), " failed to validate string \"",
                        absl::string_view(data, data_size), "\""));
    buffer_.append(data, data_size);
  }

  /**
   * Transforms the owned data using the given UnaryOperation (conforms to std::transform).
   * @param unary_op the operations to be performed on each of the elements.
   */
  template <typename UnaryOperation> void inlineTransform(UnaryOperation&& unary_op) {
    ASSERT(type() == Type::Inline);
    char* data = buffer_.mutableData();
    std::transform(data, data + buffer_.size(), data, unary_op);
  }

  /**
   * Trim trailing whitespaces from the string. Only supported by the owned representation.
   */
  void rtrim() {
    ASSERT(type() == Type::Inline);
    absl::string_view original = getStringView();
    absl::string_view rtrimmed = StringUtil::rtrim(original);
    if (original.size() != rtrimmed.size()) {
      buffer_.shrink(static_cast<uint32_t>(rtrimmed.size()));
    }
  }

  /**
   * Get an absl::string_view. It will NOT be NUL terminated!
   *
   * @return an absl::string_view.
   */
  absl::string_view getStringView() const { return {buffer_.data(), buffer_.size()}; }

  /**
   * Return the string to a default state. Reference strings are not touched. Both inline/dynamic
   * strings are reset to zero size.
   */
  void clear() { buffer_.clear(); }

  /**
   * @return whether the string is empty or not.
   */
  bool empty() const { return size() == 0; }

  // Looking for find? Use getStringView().find()

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(const char* data, uint32_t size) {
    buffer_.assign(data, size);
    assertValid();
  }

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(absl::string_view view) {
    validateCapacity(view.size());
    setCopy(view.data(), static_cast<uint32_t>(view.size()));
  }

  /**
   * Set the value of the string to an integer. This overwrites any existing string.
   */
  void setInteger(uint64_t value) {
    // Initialize the size to the max length, copy the actual data, and then
    // reduce the size (but not the capacity) as needed
    // Note: instead of using the inner_buffer, attempted the following:
    // resize buffer_ to MaxIntegerLength, apply StringUtil::itoa to the buffer_.data(), and then
    // resize buffer_ to int_length (the number of digits in value).
    // However it was slower than the following approach.
    char inner_buffer[MaxIntegerLength];
    const uint32_t int_length = StringUtil::itoa(inner_buffer, MaxIntegerLength, value);
    buffer_.assign(inner_buffer, int_length);
  }

  /**
   * Set the value of the string to a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  void setReference(absl::string_view ref_value) {
    buffer_.setReference(ref_value);
    assertValid();
  }

  /**
   * @return whether the string is a reference or owned data.
   */
  bool isReference() const {
    return buffer_.storageLocation() == UnionStringStorage::StorageLocation::Reference;
  }

  /**
   * @return the size of the string, not including the null terminator.
   */
  uint32_t size() const { return buffer_.size(); }

  bool operator==(const char* rhs) const {
    return getStringView() == absl::NullSafeStringView(rhs);
  }
  bool operator==(absl::string_view rhs) const { return getStringView() == rhs; }
  bool operator!=(const char* rhs) const {
    return getStringView() != absl::NullSafeStringView(rhs);
  }
  bool operator!=(absl::string_view rhs) const { return getStringView() != rhs; }

  // Test only method that does not have validation and allows setting arbitrary values.
  void setCopyUnvalidatedForTestOnly(absl::string_view view) {
    validateCapacity(view.size());
    buffer_.assign(view.data(), static_cast<uint32_t>(view.size()));
  }

  /**
   * @return raw Storage for cross-class move. This method is used to transfer ownership
   * between UnionString with different Validator.
   */
  Storage& storage() { return buffer_; }

protected:
  enum class Type { Reference, Inline };

  bool valid() const { return Validator()(getStringView()); }

  bool valid(absl::string_view data) const { return Validator()(data); }

  /**
   * @return the type of backing storage for the string.
   */
  Type type() const {
    return buffer_.storageLocation() == UnionStringStorage::StorageLocation::Reference
               ? Type::Reference
               : Type::Inline;
  }

  Storage buffer_;
};

class EmptyStringValidator {
public:
  bool operator()(absl::string_view) { return true; }
};

using UnionString = UnionStringBase<EmptyStringValidator>;

} // namespace Envoy
