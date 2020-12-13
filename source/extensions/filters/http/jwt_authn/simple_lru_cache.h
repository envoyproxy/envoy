#pragma once

#include <functional>

#include "absl/container/flat_hash_map.h" // for hash<>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

namespace internal {
template <typename T> struct SimpleLRUHash : public std::hash<T> {};
} // namespace internal

template <typename Key, typename Value, typename H = internal::SimpleLRUHash<Key>,
          typename EQ = std::equal_to<Key>>
class SimpleLRUCache;

// Deleter is a functor that defines how to delete a Value*. That is, it
// contains a public method:
//  operator() (Value* value)
// See example in the associated unittest.
template <typename Key, typename Value, typename Deleter, typename H = internal::SimpleLRUHash<Key>,
          typename EQ = std::equal_to<Key>>
class SimpleLRUCacheWithDeleter;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy