#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/assert.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/tls/ssl_socket.h"

#include "quiche/quic/core/crypto/quic_crypto_client_config.h"

namespace Envoy {
namespace Quic {

#define QUIC_TRANSPORT_SOCKET_FACTORY_STATS(COUNTER)                                               \
  COUNTER(context_config_update_by_sds)                                                            \
  COUNTER(upstream_context_secrets_not_ready)                                                      \
  COUNTER(downstream_context_secrets_not_ready)

struct QuicTransportSocketFactoryStats {
  QUIC_TRANSPORT_SOCKET_FACTORY_STATS(GENERATE_COUNTER_STRUCT)
};

namespace {

QuicTransportSocketFactoryStats generateStats(Stats::Scope& store, const std::string& perspective) {
  return {QUIC_TRANSPORT_SOCKET_FACTORY_STATS(
      POOL_COUNTER_PREFIX(store, fmt::format("quic_{}_transport_socket_factory.", perspective)))};
}

} // namespace

// Base class for QUIC transport socket factory.
// Because QUIC stack handles all L4 data, there is no need of a real transport
// socket for QUIC in current implementation. This factory doesn't provides a
// transport socket, instead, its derived class provides TLS context config for
// server and client.
class QuicTransportSocketFactoryBase : protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicTransportSocketFactoryBase(Stats::Scope& store, const std::string& perspective)
      : stats_(generateStats(store, perspective)) {}

  virtual ~QuicTransportSocketFactoryBase() = default;

  // To be called right after construction.
  virtual void initialize() PURE;

  // Returns the ALPN list to negotiate during the handshake.
  const std::vector<absl::string_view>& supportedAlpnProtocols() const { return supported_alpns_; }

protected:
  virtual absl::Status onSecretUpdated() PURE;
  QuicTransportSocketFactoryStats stats_;
  // Populated during initialization.
  std::vector<absl::string_view> supported_alpns_;
};

// Base class to create above QuicTransportSocketFactory for server and client
// side.
class QuicTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  // Server::Configuration::TransportSocketConfigFactory
  std::string name() const override { return "envoy.transport_sockets.quic"; }
};

} // namespace Quic
} // namespace Envoy
