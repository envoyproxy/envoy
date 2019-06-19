#pragma once

#include <deque>
#include <memory>
#include <queue>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "quiche/common/simple_linked_hash_map.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

template <typename Key> using QuicDefaultHasherImpl = absl::Hash<Key>;

template <typename Key, typename Value, typename Hash>
using QuicUnorderedMapImpl = absl::node_hash_map<Key, Value, Hash>;

template <typename Key, typename Hash> using QuicUnorderedSetImpl = absl::node_hash_set<Key, Hash>;

template <typename Key, typename Value, typename Hash>
using QuicLinkedHashMapImpl = quiche::SimpleLinkedHashMap<Key, Value, Hash>;

template <typename Key, typename Value, int Size>
using QuicSmallMapImpl = absl::flat_hash_map<Key, Value>;

template <typename T> using QuicQueueImpl = std::queue<T>;

template <typename T> using QuicDequeImpl = std::deque<T>;

template <typename T, size_t N, typename A = std::allocator<T>>
using QuicInlinedVectorImpl = absl::InlinedVector<T, N, A>;

} // namespace quic
