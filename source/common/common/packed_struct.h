#pragma once

#include <limits>
#include <type_traits>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {

/**
 * Helper class to pack elements of the same type into a struct like
 * object, with the expectation that some or most of the elements are not set.
 *
 * The overhead of using this class as opposed to declaring the elements
 * individually in another class or struct is 8 + (n+1) unaligned bytes, where n is
 * the maximum number of elements in the struct.
 *
 * The way to use this class is to first define an enum class with the element
 * names, and providing the enum class as the third template argument to
 * PackedStruct, the first two being the type of element and number of element
 * names. Instantiate the object with the number of elements that have values.
 * Note that this can be increased after the fact but can (potentially) lead to
 * memory fragmentation.
 *
 * For example:
 *
 * ```
 *    enum class MyElementNames {
 *      element0,
 *      element1,
 *      element2,
 *      element3
 *    };
 *
 *    using MyPackedStringStruct = PackedStruct<std::string, 4, MyElementNames>;
 *
 *    MyPackedStringStruct packed_struct(2); // For this instantiation, only 2
 *                                           // elements are non-empty
 *    packed_struct.set(MyElementNames::element1, "abc");
 *    packed_struct.set(MyElementNames::element2, "def");
 *
 *    // Code that accesses the elements of the struct needs to check for existence first.
 *    if (packed_struct.has(MyElementNames::element0) {
 *      auto& element0 = packed_struct.get(MyElements::element0);
 *    }
 * ```
 */
template <class T, uint8_t max_size, class ElementName> class PackedStruct {
public:
  PackedStruct(size_t a_capacity = 0) : data_(a_capacity > 0 ? new T[a_capacity] : nullptr) {
    static_assert(std::is_enum_v<ElementName>);
    static_assert(max_size > 0);

    RELEASE_ASSERT(a_capacity < std::numeric_limits<uint8_t>::max(),
                   "capacity should fit in uint8_t.");
    indices_.fill(max_size);
    indices_[max_size] = a_capacity;
  }

  T& get(ElementName element_name) {
    return (*this)[static_cast<std::underlying_type_t<ElementName>>(element_name)];
  }
  const T& get(ElementName element_name) const {
    return (*this)[static_cast<std::underlying_type_t<ElementName>>(element_name)];
  }
  bool has(ElementName element_name) const {
    return has(static_cast<std::underlying_type_t<ElementName>>(element_name));
  }
  void set(ElementName element_name, T t) {
    return assign(static_cast<std::underlying_type_t<ElementName>>(element_name), t);
  }

  // Number of non-empty elements in the struct. Note that this can be less than
  // capacity.
  size_t size() const {
    return std::accumulate(indices_.begin(), indices_.end() - 1, 0,
                           [](uint8_t a, uint8_t b) { return b < max_size ? ++a : a; });
  }

  size_t capacity() const { return indices_[max_size]; }

  // Disable copying
  PackedStruct(const PackedStruct&) = delete;

  friend void swap(PackedStruct& first, PackedStruct& second) {
    using std::swap;
    swap(first.data_, second.data_);
    swap(first.indices_, second.indices_);
  }

  // Move constructor and assignment operator.
  PackedStruct(PackedStruct&& other) noexcept : PackedStruct(0) { swap(*this, other); }
  PackedStruct& operator=(PackedStruct&& other) {
    swap(*this, other);
    return *this;
  }

  ~PackedStruct() = default;

private:
  // Accessors.
  T& operator[](size_t idx) {
    RELEASE_ASSERT(idx < static_cast<size_t>(max_size),
                   absl::StrCat("idx (", idx, ") should be less than ", max_size));
    RELEASE_ASSERT(indices_[idx] < max_size,
                   absl::StrCat("Element corresponding to index ", idx, " is not assigned"));
    return (data_.get())[indices_[idx]];
  }
  const T& operator[](size_t idx) const {
    RELEASE_ASSERT(idx < static_cast<size_t>(max_size),
                   absl::StrCat("idx (", idx, ") should be less than ", max_size));
    RELEASE_ASSERT(indices_[idx] < max_size,
                   absl::StrCat("Element corresponding to index ", idx, " is not assigned"));
    return (data_.get())[indices_[idx]];
  }

  bool has(size_t idx) const {
    RELEASE_ASSERT(idx < static_cast<size_t>(max_size),
                   absl::StrCat("idx (", idx, ") should be less than ", max_size));
    return indices_[idx] < max_size;
  }

  void assign(size_t element_idx, T t) {
    RELEASE_ASSERT(element_idx < static_cast<size_t>(max_size),
                   absl::StrCat("element_idx (", element_idx, ") should be less than ", max_size));
    auto const current_size = size();
    // If we're at capacity and we don't have a slot for element_idx, increase capacity by 1.
    if (!has(element_idx) && current_size == capacity()) {
      std::unique_ptr<T[]> tmp(new T[++indices_[max_size]]);
      for (size_t idx = 0; idx < current_size; ++idx) {
        swap(tmp.get()[idx], data_.get()[idx]);
      }
      swap(data_, tmp);
    }
    if (!has(element_idx)) {
      indices_[element_idx] = current_size;
    }
    data_.get()[indices_[element_idx]] = t;
  }

  // Storage for elements.
  std::unique_ptr<T[]> data_;

  // Use indices_[max_size] to store capacity.
  std::array<uint8_t, static_cast<size_t>(max_size) + 1> indices_;
};

} // namespace Envoy
