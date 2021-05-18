#pragma once

#include "envoy/config/typed_config.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/quic/core/quic_session.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamFactory : public Config::TypedFactory {
 public:
  std::string category() const override { return "quic.server.crypto_stream"; }

  // Return an Envoy specific quic crypto server stream object.
  virtual std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache,
    quic::QuicSession* session, quic::QuicCryptoServerStreamBase::Helper* helper) PURE;
};

}  // namespace Quic
}  // namespace Envoy
