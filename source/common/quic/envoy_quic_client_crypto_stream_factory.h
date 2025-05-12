#pragma once

#include "envoy/common/optref.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/transport_socket.h"

#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoClientStreamFactoryInterface {
public:
  virtual ~EnvoyQuicCryptoClientStreamFactoryInterface() = default;

  // Return an Envoy specific quic crypto client stream object.
  virtual std::unique_ptr<quic::QuicCryptoClientStreamBase>
  createEnvoyQuicCryptoClientStream(const quic::QuicServerId& server_id, quic::QuicSession* session,
                                    std::unique_ptr<quic::ProofVerifyContext> verify_context,
                                    quic::QuicCryptoClientConfig* crypto_config,
                                    quic::QuicCryptoClientStream::ProofHandler* proof_handler,
                                    bool has_application_state) PURE;
};

} // namespace Quic
} // namespace Envoy
