#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_client_stream.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoClientStreamBase>
EnvoyQuicCryptoClientStreamFactoryImpl::createEnvoyQuicCryptoClientStream(
    const quic::QuicServerId& server_id, quic::QuicSession* session,
    std::unique_ptr<quic::ProofVerifyContext> verify_context,
    quic::QuicCryptoClientConfig* crypto_config,
    quic::QuicCryptoClientStream::ProofHandler* proof_handler, bool has_application_state) {
  return std::make_unique<quic::QuicCryptoClientStream>(server_id, session,
                                                        std::move(verify_context), crypto_config,
                                                        proof_handler, has_application_state);
};

} // namespace Quic
} // namespace Envoy
