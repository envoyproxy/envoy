#pragma once

#include <algorithm>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {

/**
 * Convenient type for an inline vector that will be used by InlinedString.
 */
using InlinedStringVector = absl::InlinedVector<char, 128>;

/**
 * Convenient type for the underlying type of InlinedString that allows a variant
 * between string_view and the InlinedVector.
 */
using VariantStringOrView = absl::variant<absl::string_view, InlinedStringVector>;

// This includes the NULL (StringUtil::itoa technically only needs 21).
inline constexpr size_t MaxIntegerLength{32};

inline void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
}

inline absl::string_view getStrView(const VariantStringOrView& buffer) {
  return absl::get<absl::string_view>(buffer);
}

inline InlinedStringVector& getInVec(VariantStringOrView& buffer) {
  return absl::get<InlinedStringVector>(buffer);
}

inline const InlinedStringVector& getInVec(const VariantStringOrView& buffer) {
  return absl::get<InlinedStringVector>(buffer);
}

/**
 * This is a string implementation that unified string reference and owned string. It is heavily
 * optimized for performance. It supports 2 different types of storage and can switch between them:
 * 1) A string reference.
 * 2) A string InlinedVector (an optimized interned string for small strings, but allows heap
 * allocation if needed).
 */
template <class Validator> class UnionStringBase {
public:
  using Storage = VariantStringOrView;

  /**
   * Default constructor. Sets up for inline storage.
   */
  UnionStringBase() : buffer_(InlinedStringVector()) {
    ASSERT((getInVec(buffer_).capacity()) >= MaxIntegerLength);
    ASSERT(valid());
  }

  /**
   * Constructor for a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  explicit UnionStringBase(absl::string_view ref_value) : buffer_(ref_value) { ASSERT(valid()); }

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
    const uint64_t new_capacity = static_cast<uint64_t>(data_size) + size();
    validateCapacity(new_capacity);
    ASSERT(valid(absl::string_view(data, data_size)));

    switch (type()) {
    case Type::Reference: {
      // Rather than be too clever and optimize this uncommon case, we switch to
      // Inline mode and copy.
      const absl::string_view prev = getStrView(buffer_);
      buffer_ = InlinedStringVector();
      // Assigning new_capacity to avoid resizing when appending the new data
      getInVec(buffer_).reserve(new_capacity);
      getInVec(buffer_).assign(prev.begin(), prev.end());
      break;
    }
    case Type::Inline: {
      getInVec(buffer_).reserve(new_capacity);
      break;
    }
    }
    getInVec(buffer_).insert(getInVec(buffer_).end(), data, data + data_size);
  }

  /**
   * Transforms the inlined vector data using the given UnaryOperation (conforms
   * to std::transform).
   * @param unary_op the operations to be performed on each of the elements.
   */
  template <typename UnaryOperation> void inlineTransform(UnaryOperation&& unary_op) {
    ASSERT(type() == Type::Inline);
    std::transform(absl::get<InlinedStringVector>(buffer_).begin(),
                   absl::get<InlinedStringVector>(buffer_).end(),
                   absl::get<InlinedStringVector>(buffer_).begin(), unary_op);
  }

  /**
   * Trim trailing whitespaces from the InlinedString. Only supported by the "Inline" InlinedString
   * representation.
   */
  void rtrim() {
    ASSERT(type() == Type::Inline);
    absl::string_view original = getStringView();
    absl::string_view rtrimmed = StringUtil::rtrim(original);
    if (original.size() != rtrimmed.size()) {
      getInVec(buffer_).resize(rtrimmed.size());
    }
  }

  /**
   * Get an absl::string_view. It will NOT be NUL terminated!
   *
   * @return an absl::string_view.
   */
  absl::string_view getStringView() const {
    if (type() == Type::Reference) {
      return getStrView(buffer_);
    }
    ASSERT(type() == Type::Inline);
    return {getInVec(buffer_).data(), getInVec(buffer_).size()};
  }

  /**
   * Return the string to a default state. Reference strings are not touched. Both inline/dynamic
   * strings are reset to zero size.
   */
  void clear() {
    if (type() == Type::Inline) {
      getInVec(buffer_).clear();
    }
  }

  /**
   * @return whether the string is empty or not.
   */
  bool empty() const { return size() == 0; }

  // Looking for find? Use getStringView().find()

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(const char* data, uint32_t size) {
    if (!absl::holds_alternative<InlinedStringVector>(buffer_)) {
      // Switching from Type::Reference to Type::Inline
      buffer_ = InlinedStringVector();
    }

    getInVec(buffer_).reserve(size);
    getInVec(buffer_).assign(data, data + size);
    ASSERT(valid());
  }

  /**
   * Set the value of the string by copying data into it. This overwrites any existing string.
   */
  void setCopy(absl::string_view view) { setCopy(view.data(), view.size()); }

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

    if (type() == Type::Reference) {
      // Switching from Type::Reference to Type::Inline
      buffer_ = InlinedStringVector();
    }
    ASSERT((getInVec(buffer_).capacity()) > MaxIntegerLength);
    getInVec(buffer_).assign(inner_buffer, inner_buffer + int_length);
  }

  /**
   * Set the value of the string to a string reference.
   * @param ref_value MUST point to data that will live beyond the lifetime of any request/response
   *        using the string (since a codec may optimize for zero copy).
   */
  void setReference(absl::string_view ref_value) {
    buffer_ = ref_value;
    ASSERT(valid());
  }

  /**
   * @return whether the string is a reference or an InlinedVector.
   */
  bool isReference() const { return type() == Type::Reference; }

  /**
   * @return the size of the string, not including the null terminator.
   */
  uint32_t size() const {
    if (type() == Type::Reference) {
      return getStrView(buffer_).size();
    }
    ASSERT(type() == Type::Inline);
    return getInVec(buffer_).size();
  }

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
    if (!absl::holds_alternative<InlinedStringVector>(buffer_)) {
      // Switching from Type::Reference to Type::Inline
      buffer_ = InlinedStringVector();
    }

    getInVec(buffer_).reserve(view.size());
    getInVec(buffer_).assign(view.data(), view.data() + view.size());
  }

  /**
   * @return raw Storage for cross-class move. This method is used to tranfer ownership
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
    // buffer_.index() is correlated with the order of Reference and Inline in the
    // enum.
    ASSERT((buffer_.index() == 0) || (buffer_.index() == 1));
    ASSERT((buffer_.index() == 0 && absl::holds_alternative<absl::string_view>(buffer_)) ||
           (buffer_.index() != 0));
    ASSERT((buffer_.index() == 1 && absl::holds_alternative<InlinedStringVector>(buffer_)) ||
           (buffer_.index() != 1));
    return Type(buffer_.index());
  }

  Storage buffer_;
};

class EmptyStringValidator {
public:
  bool operator()(absl::string_view) { return true; }
};

using UnionString = UnionStringBase<EmptyStringValidator>;

} // namespace Envoy
