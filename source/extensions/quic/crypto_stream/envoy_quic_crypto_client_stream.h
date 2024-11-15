#pragma once

#include "source/common/quic/envoy_quic_client_crypto_stream_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoClientStreamFactoryImpl : public EnvoyQuicCryptoClientStreamFactoryInterface {
public:
  std::unique_ptr<quic::QuicCryptoClientStreamBase>
  createEnvoyQuicCryptoClientStream(const quic::QuicServerId& server_id, quic::QuicSession* session,
                                    std::unique_ptr<quic::ProofVerifyContext> verify_context,
                                    quic::QuicCryptoClientConfig* crypto_config,
                                    quic::QuicCryptoClientStream::ProofHandler* proof_handler,
                                    bool has_application_state) override;
};

} // namespace Quic
} // namespace Envoy
