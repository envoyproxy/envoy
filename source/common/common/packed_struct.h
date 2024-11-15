#pragma once

#include <algorithm>
#include <limits>
#include <type_traits>

#include "envoy/common/optref.h"

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
 * the maximum number of elements (max_size) in the struct.
 *
 * The way to use this class is to first define an enum class with the element
 * names, and providing the enum class as the third template argument to
 * PackedStruct, the first two being the type of element and number of element
 * names respectively. Instantiate the object with the number of elements that have values.
 * Note that this can be increased after the fact but can (potentially) lead to
 * memory fragmentation.
 *
 * For example:
 *
 * ```
 *    enum class MyElementNames {
 *      element0 = 0,
 *      element1,
 *      element2,
 *      element3
 *    };
 *
 *    using MyPackedStringStruct = PackedStruct<std::string, 4, MyElementNames>;
 *
 *    MyPackedStringStruct packed_struct(2); // For this instantiation, only 2
 *                                           // elements are non-empty
 *    packed_struct.set<MyElementNames::element1>("abc");
 *    packed_struct.set<MyElementNames::element2>("def");
 *
 *    // Code that accesses the elements of the struct needs to check for existence first.
 *    auto element0 = packed_struct.get<MyElements::element0>();
 *    if (element0.has_value()) {
 *      // use element0.ref()
 *    }
 *
 * ```
 *
 * Implementation details:
 *
 * This class stores all elements in the member data_ which is a
 * unique_ptr<T[]>. Using a unique_ptr<T[]> instead of vector<T> saves us 16
 * bytes.
 *
 * The class can store 0 or 1 value corresponding to each element of the enum
 * ElementName.
 *
 * If an element has a value, then its corresponding index in
 * data_ is given by indices_[static_cast<unsigned>(element_name)], where
 * element_name is the corresponding enum value. If an element does not have a
 * value, then indices_[static_cast<unsigned>(element_name)] = max_size.
 *
 * We calculate the size of the class (i.e. number of elements stored) by
 * counting all but the last entry in indices_ that are less than max_size. The
 * last entry in indices_ is reserved to store the size of data_.
 * We grow data_ (always by 1) only when adding an element when
 * number of elements == size of data_.
 *
 * Note that it is only possible to add or modify an element, but not to remove
 * an element.
 *
 */
template <class T, uint8_t max_size, class ElementName> class PackedStruct {
public:
  PackedStruct(size_t a_capacity = 0)
      : data_(a_capacity > 0 ? std::make_unique<T[]>(a_capacity) : nullptr) {
    static_assert(std::is_enum_v<ElementName>);
    static_assert(max_size > 0);

    RELEASE_ASSERT(a_capacity <= max_size,
                   absl::StrCat("capacity should be less than or equal to ", max_size, "."));

    // Initialize indices with max_size as we start with 0 elements.
    indices_.fill(max_size);
    // Set the current capacity.
    indices_[max_size] = a_capacity;
  }

  // Accessors.
  template <ElementName element_name> Envoy::OptRef<T> get() {
    sizeCheck<element_name>();
    if (!has<element_name>()) {
      return Envoy::OptRef<T>{};
    }
    return Envoy::makeOptRef<T>(
        data_.get()[indices_[static_cast<std::underlying_type_t<ElementName>>(element_name)]]);
  }

  template <ElementName element_name> const Envoy::OptRef<const T> get() const {
    sizeCheck<element_name>();
    if (!has<element_name>()) {
      return Envoy::OptRef<const T>{};
    }
    return Envoy::makeOptRef<const T>(
        data_.get()[indices_[static_cast<std::underlying_type_t<ElementName>>(element_name)]]);
  }

  // Set element.
  template <ElementName element_name> void set(T t) {
    sizeCheck<element_name>();

    // Cast the enum to an integer for looking up the corresponding index in
    // data_;
    static constexpr auto element_idx =
        static_cast<std::underlying_type_t<ElementName>>(element_name);

    // Get the current size. Note that this can be less than capacity.
    auto const current_size = size();

    // If we're at capacity and we don't have a slot for element_name, increase capacity by 1.
    if (!has<element_name>() && current_size == capacity()) {
      auto tmp = std::make_unique<T[]>(++indices_[max_size]);
      std::move(data_.get(), std::next(data_.get(), current_size), tmp.get());
      data_ = std::move(tmp);
    }
    // If we don't have a slot for element_name in data_, allot the slot
    // corresponding to the value of current_size.
    if (!has<element_name>()) {
      indices_[element_idx] = current_size;
    }
    // Set the value for element_name in the corresponding slot, which is
    // indices_[element_idx]
    data_.get()[indices_[element_idx]] = t;
  }

  // Number of non-empty elements in the struct. Note that this can be less than
  // capacity.
  size_t size() const {
    return std::count_if(indices_.begin(), indices_.end() - 1,
                         [](uint8_t a) { return a < max_size; });
  }

  // Disable copying
  PackedStruct(const PackedStruct&) = delete;

  // Move constructor and assignment operator.
  PackedStruct(PackedStruct&& other) noexcept = default;
  PackedStruct& operator=(PackedStruct&& other) noexcept = default;

  friend class PackedStructTest;

private:
  size_t capacity() const { return indices_[max_size]; }

  // Check to see if element is populated.
  template <ElementName element_name> bool has() const {
    sizeCheck<element_name>();
    return indices_[static_cast<std::underlying_type_t<ElementName>>(element_name)] < max_size;
  }

  template <ElementName element_name> static constexpr void sizeCheck() {
    // Elements of ElementName cast to size_t should be less than max_size.
    static_assert(static_cast<size_t>(element_name) < static_cast<size_t>(max_size));
  }

  // Storage for elements.
  std::unique_ptr<T[]> data_;

  // Use indices_[max_size] to store capacity.
  std::array<uint8_t, static_cast<size_t>(max_size) + 1> indices_;
};

} // namespace Envoy
