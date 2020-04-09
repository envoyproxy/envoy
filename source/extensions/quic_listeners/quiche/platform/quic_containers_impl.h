#pragma once

#include <deque>
#include <memory>
#include <ostream>
#include <queue>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "quiche/common/simple_linked_hash_map.h"
#include "quiche/quic/platform/api/quic_flags.h"

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

template <typename T, size_t N, typename A>
inline std::ostream& operator<<(std::ostream& os,
                                const QuicInlinedVectorImpl<T, N, A> inlined_vector) {
  std::stringstream debug_string;
  debug_string << "{";
  typename QuicInlinedVectorImpl<T, N, A>::const_iterator it = inlined_vector.cbegin();
  debug_string << *it;
  ++it;
  while (it != inlined_vector.cend()) {
    debug_string << ", " << *it;
    ++it;
  }
  debug_string << "}";
  return os << debug_string.str();
}

} // namespace quic
