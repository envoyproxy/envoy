#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
/**
 * These are helper macros to pack elements of the same type into a struct like
 * object, with the expectation that some or most of the elements are not set.
 *
 * The overhead of using these macros is 8 + (n+2) unaligned bytes, where n is
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

/**
 * Helper class to store elements in an array. Overhead of using this class is 8
 * + 2 unaligned bytes.
 */
template <class T> class PackedStruct {
public:
  PackedStruct(size_t capacity = 0)
      : data_(capacity > 0 ? new T[capacity] : nullptr), capacity_(capacity) {
    RELEASE_ASSERT(capacity < 0x7f, "size must be less than 0x7f");
  }

  // Accessors.
  T& operator[](size_t idx) {
    RELEASE_ASSERT(idx < size_,
                   absl::StrCat("Index ", idx, " is out of bounds. Size is (", size_, ")"));
    return (data_.get())[idx];
  }
  const T& operator[](size_t idx) const {
    RELEASE_ASSERT(idx < size_,
                   absl::StrCat("Index ", idx, " is out of bounds. Size is (", size_, ")"));
    return (data_.get())[idx];
  }

  // Disable copying
  PackedStruct(const PackedStruct&) = delete;

  friend void swap(PackedStruct& first, PackedStruct& second) {
    using std::swap;
    swap(first.data_, second.data_);
    swap(first.size_, second.size_);
    swap(first.capacity_, second.capacity_);
  }

  PackedStruct(PackedStruct&& other) noexcept : PackedStruct(0) { swap(*this, other); }

  void push_back(T t) {
    RELEASE_ASSERT(size_ == 0 || size_ - 1 < 0x7f, "size must be less than 0x7f");
    // If we're at capacity, increase capacity by 1.
    if (size_ == capacity_) {
      std::unique_ptr<T[]> tmp(new T[++capacity_]);
      for (size_t idx = 0; idx < size_; ++idx) {
        swap(tmp.get()[idx], data_.get()[idx]);
      }
      swap(data_, tmp);
    }
    data_.get()[size_++] = t;
  }

  uint8_t size() const { return size_; }
  uint8_t capacity() const { return capacity_; }

private:
  std::unique_ptr<T[]> data_;
  uint8_t size_ = 0;
  uint8_t capacity_ = 0;
};

#define GENERATE_PACKED_STRUCT_ACCESSOR(NAME, ...)                                                 \
  const Type& NAME() const {                                                                       \
    RELEASE_ASSERT(NAME##_idx_ != -1, #NAME "_ is unset.");                                        \
    return packed_struct_[NAME##_idx_];                                                            \
  }                                                                                                \
  bool has_##NAME() const { return NAME##_idx_ != -1; }

#define GENERATE_PACKED_STRUCT_OPTIONAL_ACCESSOR(NAME, ...)                                        \
  absl::optional<Type> NAME() const {                                                              \
    return NAME##_idx_ != -1 ? absl::optional<Type>(packed_struct_[NAME##_idx_]) : absl::nullopt;  \
  }

#define GENERATE_PACKED_STRUCT_IDX(NAME, ...) int8_t NAME##_idx_ = -1;

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
  void set_##NAME(Type t) {                                                                        \
    if (NAME##_idx_ == -1) {                                                                       \
      NAME##_idx_ = packed_struct_.size();                                                         \
      packed_struct_.push_back(t);                                                                 \
      return;                                                                                      \
    }                                                                                              \
    packed_struct_[NAME##_idx_] = t;                                                               \
  }

#define MAKE_PACKED_STRUCT_FROM_PROTO(PackedStructName, ALL_ELEMENTS, ACCESSOR, HAS_CHECK,         \
                                      ASSIGNMENT)                                                  \
  template <class Type, class ProtoType> struct PackedStructName {                                 \
  public:                                                                                          \
    ALL_ELEMENTS(ACCESSOR)                                                                         \
    PackedStructName() = delete;                                                                   \
                                                                                                   \
    PackedStructName(const ProtoType& proto) : packed_struct_(getSize(proto)) {                    \
      ALL_ELEMENTS(ASSIGNMENT)                                                                     \
    }                                                                                              \
                                                                                                   \
  private:                                                                                         \
    uint64_t getSize(const ProtoType& proto) {                                                     \
      size_t size = 0;                                                                             \
      ALL_ELEMENTS(HAS_CHECK)                                                                      \
      return size;                                                                                 \
    }                                                                                              \
                                                                                                   \
    ALL_ELEMENTS(GENERATE_PACKED_STRUCT_SETTER)                                                    \
    PackedStruct<Type> packed_struct_;                                                             \
    ALL_ELEMENTS(GENERATE_PACKED_STRUCT_IDX)                                                       \
  }

#define MAKE_PACKED_STRUCT_BASE(PackedStructName, ALL_ELEMENTS, ACCESSOR, SETTER)                  \
  template <class Type> struct PackedStructName {                                                  \
  public:                                                                                          \
    ALL_ELEMENTS(ACCESSOR)                                                                         \
    ALL_ELEMENTS(SETTER)                                                                           \
    PackedStructName() = delete;                                                                   \
    PackedStructName(size_t size) : packed_struct_(size) {}                                        \
                                                                                                   \
  private:                                                                                         \
    PackedStruct<Type> packed_struct_;                                                             \
    ALL_ELEMENTS(GENERATE_PACKED_STRUCT_IDX)                                                       \
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
