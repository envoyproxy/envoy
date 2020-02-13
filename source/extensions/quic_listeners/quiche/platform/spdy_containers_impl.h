#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/hash/hash.h"
#include "quiche/common/simple_linked_hash_map.h"

namespace spdy {

template <typename KeyType> using SpdyHashImpl = absl::Hash<KeyType>;

template <typename KeyType, typename ValueType, typename Hash = absl::Hash<KeyType>>
using SpdyHashMapImpl = absl::flat_hash_map<KeyType, ValueType, Hash>;

template <typename ElementType, typename Hasher, typename Eq>
using SpdyHashSetImpl = absl::flat_hash_set<ElementType, Hasher, Eq>;

template <typename Key, typename Value, typename Hash, typename Eq>
using SpdyLinkedHashMapImpl = quiche::SimpleLinkedHashMap<Key, Value, Hash, Eq>;

template <typename T, size_t N, typename A = std::allocator<T>>
using SpdyInlinedVectorImpl = absl::InlinedVector<T, N, A>;

template <typename Key, typename Value, int Size>
using SpdySmallMapImpl = absl::flat_hash_map<Key, Value>;
} // namespace spdy
