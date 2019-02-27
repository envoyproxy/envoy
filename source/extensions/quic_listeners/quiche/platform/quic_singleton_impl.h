#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/singleton/threadsafe_singleton.h"

namespace quic {

template <typename T> class QuicSingletonImpl {
public:
  static T* get() { return &Envoy::ThreadSafeSingleton<T>::get(); }
};

template <typename T> using QuicSingletonFriendImpl = Envoy::ThreadSafeSingleton<T>;

} // namespace quic
