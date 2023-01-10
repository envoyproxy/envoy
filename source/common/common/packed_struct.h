#pragma once

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
 *      auto element0 = packed_struct.get(MyElements::element0);
 *    }
 * ```
 */
template <class T, uint8_t max_size, class ElementName> class PackedStruct {
public:
  PackedStruct(size_t a_capacity = 0) : data_(a_capacity > 0 ? new T[a_capacity] : nullptr) {
    static_assert(std::is_enum_v<ElementName>);
    static_assert(max_size > 0);

    indices_.fill(max_size);
    indices_[max_size] = a_capacity;
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

/**
 * These are helper macros to pack elements of the same type into a struct like
 * object, with the expectation that some or most of the elements are not set.
 * The main advantage of using these instead of PackedStruct directly is that
 * one does not need to define enums for element names, and accessors and
 * modifiers for elements are more intuitive to use (if one can get over the
 * complexity arising from having to use macros).
 *
 * The overhead of using these macros is 8 + (n+1) unaligned bytes, where n is
 * the maximum number of elements in the struct.
 *
 * The general flow looks like this:
 *
 * Define a block of elements as such:
 *   #define MY_PACKED_STRUCT(HELPER) \
 *     HELPER(element1) \
 *     HELPER(element2) \
 *     HELPER(element3) \
 *     HELPER(element4) \
 *     ...
 *
 * Define the templated class that will contain the above elements as such:
 *   MAKE_PACKED_STRUCT(MyPackedStruct, MY_PACKED_STRUCT);
 *
 * Declare and instantiate the object as such:
 *   MyPackedStruct<std::string> my_packed_struct(2);
 *   my_packed_struct.set_element2("abc");
 *   my_packed_struct.set_element4("def");
 *
 * Use the object as such:
 *  if (my_packed_struct.has_element2()) {
 *  // do something with my_packed_struct.element2();
 *  }
 *
 *  There are also helper macros for structs with elements of type std::string
 *  and std::chrono::milliseconds, where the elements correspond directly to
 *  proto elements. The constructors for these structs take the proto as an
 *  argument and their elements don't have to be set manually.
 *
 *  For example:
 *
 *  #define REDIRECT_STRING_ELEMENTS(HELPER) \
 *    HELPER(scheme_redirect) \
 *    HELPER(host_redirect) \
 *    HELPER(path_redirect)
 *
 *  MAKE_PACKED_STRING_STRUCT(RedirectStringsFromProto, REDIRECT_STRING_ELEMENTS);
 *
 *  #define TIMEOUT_ELEMENTS(HELPER) \
 *    HELPER(idle_timeout) \
 *    HELPER(max_grpc_timeout) \
 *    HELPER(grpc_timeout_offset)
 *
 *  MAKE_PACKED_MS_STRUCT(TimeoutsFromProto, TIMEOUT_ELEMENTS);
 *
 *   envoy::config::route::v3::RedirectAction redirect_action;
 *   // populate redirect_action here
 *
 *   // Create packed structs from the proto.
 *   RedirectStringsFromProto<std::string, envoy::config::route::v3::RedirectAction>
 *       redirect_strings( redirect_action);
 *   TimeoutsFromProto<std::chrono::milliseconds, envoy::config::route::v3::RouteAction> timeouts(
      route);
 */

#define GENERATE_PACKED_STRUCT_ACCESSOR(NAME, ...)                                                 \
  const Type& NAME() const { return data_.get(Elements::NAME); }                                   \
  Type& NAME() { return data_.get(Elements::NAME); }                                               \
  bool has_##NAME() const { return data_.has(Elements::NAME); }

#define GENERATE_PACKED_STRUCT_OPTIONAL_ACCESSOR(NAME, ...)                                        \
  absl::optional<Type> NAME() const {                                                              \
    return data_.has(Elements::NAME) ? absl::optional<Type>(data_.get(Elements::NAME))             \
                                     : absl::nullopt;                                              \
  }

#define GENERATE_PACKED_STRUCT_ENUM(ALL_ELEMENTS, ...)                                             \
  enum class Elements : uint8_t {                                                                  \
    ALL_ELEMENTS(GENERATE_PACKED_STRUCT_ENUM_HELPER) packed_struct_max_elements                    \
  };

#define GENERATE_PACKED_STRUCT_ENUM_HELPER(NAME, ...) NAME,

#define GENERATE_PACKED_STRUCT_CHECK(NAME, ...)                                                    \
  if (!proto.NAME().empty()) {                                                                     \
    size++;                                                                                        \
  }

#define GENERATE_PACKED_STRUCT_HAS_CHECK(NAME, ...)                                                \
  if (proto.has_##NAME()) {                                                                        \
    size++;                                                                                        \
  }

#define GENERATE_PACKED_STRUCT_ASSIGNMENTS(NAME, ...)                                              \
  if (!proto.NAME().empty()) {                                                                     \
    set_##NAME(proto.NAME());                                                                      \
  }

#define GENERATE_PACKED_STRUCT_MS_ASSIGNMENTS(NAME, ...)                                           \
  if (proto.has_##NAME()) {                                                                        \
    set_##NAME(std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(proto, NAME)));                  \
  }

#define GENERATE_PACKED_STRUCT_SETTER(NAME, ...)                                                   \
  void set_##NAME(Type t) { data_.set(Elements::NAME, t); }

#define MAKE_PACKED_STRUCT_FROM_PROTO(PackedStructName, ALL_ELEMENTS, ACCESSOR, HAS_CHECK,         \
                                      ASSIGNMENT)                                                  \
  template <class Type, class ProtoType> struct PackedStructName {                                 \
  public:                                                                                          \
    ALL_ELEMENTS(ACCESSOR)                                                                         \
    PackedStructName() = delete;                                                                   \
                                                                                                   \
    PackedStructName(const ProtoType& proto) : data_(getSize(proto)) {                             \
      ;                                                                                            \
      ALL_ELEMENTS(ASSIGNMENT)                                                                     \
    }                                                                                              \
                                                                                                   \
    GENERATE_PACKED_STRUCT_ENUM(ALL_ELEMENTS) private : uint64_t getSize(const ProtoType& proto) { \
      size_t size = 0;                                                                             \
      ALL_ELEMENTS(HAS_CHECK)                                                                      \
      return size;                                                                                 \
    }                                                                                              \
                                                                                                   \
    ALL_ELEMENTS(GENERATE_PACKED_STRUCT_SETTER)                                                    \
    PackedStruct<                                                                                  \
        Type, static_cast<std::underlying_type_t<Elements>>(Elements::packed_struct_max_elements), \
        Elements>                                                                                  \
        data_;                                                                                     \
  }

#define MAKE_PACKED_STRUCT_BASE(PackedStructName, ALL_ELEMENTS, ACCESSOR, SETTER)                  \
  template <class Type> struct PackedStructName {                                                  \
  public:                                                                                          \
    ALL_ELEMENTS(ACCESSOR)                                                                         \
    ALL_ELEMENTS(SETTER)                                                                           \
    PackedStructName() = delete;                                                                   \
    PackedStructName(size_t size) : data_(size) {}                                                 \
                                                                                                   \
    GENERATE_PACKED_STRUCT_ENUM(ALL_ELEMENTS)                                                      \
  private:                                                                                         \
    PackedStruct<                                                                                  \
        Type, static_cast<std::underlying_type_t<Elements>>(Elements::packed_struct_max_elements), \
        Elements>                                                                                  \
        data_;                                                                                     \
  }

#define MAKE_PACKED_STRING_STRUCT(PackedStructName, ALL_ELEMENTS)                                  \
  MAKE_PACKED_STRUCT_FROM_PROTO(PackedStructName, ALL_ELEMENTS, GENERATE_PACKED_STRUCT_ACCESSOR,   \
                                GENERATE_PACKED_STRUCT_CHECK, GENERATE_PACKED_STRUCT_ASSIGNMENTS)

#define MAKE_PACKED_MS_STRUCT(PackedStructName, ALL_ELEMENTS)                                      \
  MAKE_PACKED_STRUCT_FROM_PROTO(                                                                   \
      PackedStructName, ALL_ELEMENTS, GENERATE_PACKED_STRUCT_OPTIONAL_ACCESSOR,                    \
      GENERATE_PACKED_STRUCT_HAS_CHECK, GENERATE_PACKED_STRUCT_MS_ASSIGNMENTS)

#define MAKE_PACKED_STRUCT(PackedStructName, ALL_ELEMENTS)                                         \
  MAKE_PACKED_STRUCT_BASE(PackedStructName, ALL_ELEMENTS, GENERATE_PACKED_STRUCT_ACCESSOR,         \
                          GENERATE_PACKED_STRUCT_SETTER)

#define MAKE_PACKED_OPTIONAL_STRUCT(PackedStructName, ALL_ELEMENTS)                                \
  MAKE_PACKED_STRUCT_BASE(PackedStructName, ALL_ELEMENTS,                                          \
                          GENERATE_PACKED_STRUCT_OPTIONAL_ACCESSOR, GENERATE_PACKED_STRUCT_SETTER)

} // namespace Envoy
