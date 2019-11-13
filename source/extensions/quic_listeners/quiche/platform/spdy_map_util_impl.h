#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <algorithm>

namespace spdy {

template <class Collection, class Key>
bool SpdyContainsKeyImpl(const Collection& collection, const Key& key) {
  return collection.find(key) != collection.end();
}

} // namespace spdy
