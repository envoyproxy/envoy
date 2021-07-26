#pragma once

#include <deque>
#include <memory>
#include <ostream>
#include <queue>
#include <sstream>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "quiche/common/quiche_linked_hash_map.h"
#include "quiche/quic/platform/api/quic_flags.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

template <typename Key, typename Compare>
using QuicSmallOrderedSetImpl = absl::btree_set<Key, Compare>;

} // namespace quic
