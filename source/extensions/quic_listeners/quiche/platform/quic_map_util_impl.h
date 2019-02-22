#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <algorithm>

namespace quic {

template <class Collection, class Key>
bool QuicContainsKeyImpl(const Collection& collection, const Key& key) {
  return collection.find(key) != collection.end();
}

template <typename Collection, typename Value>
bool QuicContainsValueImpl(const Collection& collection, const Value& value) {
  return std::find(collection.begin(), collection.end(), value) != collection.end();
}

} // namespace quic
