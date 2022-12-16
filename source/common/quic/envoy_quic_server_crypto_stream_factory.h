#pragma once

#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/transport_socket.h"

#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamFactoryInterface : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.server.crypto_stream"; }

  // Return an Envoy specific quic crypto server stream object.
  virtual std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
      Event::Dispatcher& dispatcher) PURE;
};

} // namespace Quic
} // namespace Envoy
