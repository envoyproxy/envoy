#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {

/**
 * Well-known QUIC listener names.
 * NOTE: New filters should use the well known name: envoy.filters.listener.name.
 */
class QuicListenerNameValues {
public:
  // QUICHE-based implementation
  const std::string Quiche = "envoy.quic_listeners.quiche";
  // ngtcp2-based implementation
  // const std::string Ngtcp2 = "envoy.quic_listeners.ngtcp2";
};

typedef ConstSingleton<QuicListenerNameValues> QuicListenerNames;

} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
