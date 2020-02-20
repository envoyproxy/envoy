#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/container/node_hash_map.h"
#include "absl/hash/hash.h"

namespace quiche {

// The default hasher used by hash tables.
template <typename Key> using QuicheDefaultHasherImpl = absl::Hash<Key>;

// Similar to std::unordered_map, but with better performance and memory usage.
template <typename Key, typename Value, typename Hash, typename Eq>
using QuicheUnorderedMapImpl = absl::node_hash_map<Key, Value, Hash, Eq>;

} // namespace quiche
