#pragma once

#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/transport_socket.h"

#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"

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

  // Whether crypto streams created by this factory validate client certificates against the
  // filter chain's downstream TLS context. Connections on filter chains that require client
  // certificates are rejected (fail closed) when the configured factory returns false, since
  // QUICHE would otherwise accept the requested client certificate without any validation.
  virtual bool supportsClientCertificateAuthentication() const { return false; }
};

} // namespace Quic
} // namespace Envoy
