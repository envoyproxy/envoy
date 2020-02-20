#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quiche {

template <class Collection, class Key>
bool QuicheContainsKeyImpl(const Collection& collection, const Key& key) {
  return collection.find(key) != collection.end();
}

} // namespace quiche
