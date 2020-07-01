#include "extensions/quic_listeners/quiche/envoy_quic_proof_source_base.h"

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "quiche/quic/core/quic_data_writer.h"

#pragma GCC diagnostic pop

namespace Envoy {
namespace Quic {

void EnvoyQuicProofSourceBase::GetProof(const quic::QuicSocketAddress& server_address,
                                        const quic::QuicSocketAddress& client_address,
                                        const std::string& hostname,
                                        const std::string& server_config,
                                        quic::QuicTransportVersion /*transport_version*/,
                                        quiche::QuicheStringPiece chlo_hash,
                                        std::unique_ptr<quic::ProofSource::Callback> callback) {
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain =
      GetCertChain(server_address, client_address, hostname);

  size_t payload_size = sizeof(quic::kProofSignatureLabel) + sizeof(uint32_t) + chlo_hash.size() +
                        server_config.size();
  auto payload = std::make_unique<char[]>(payload_size);
  quic::QuicDataWriter payload_writer(payload_size, payload.get(),
                                      quiche::Endianness::HOST_BYTE_ORDER);
  bool success =
      payload_writer.WriteBytes(quic::kProofSignatureLabel, sizeof(quic::kProofSignatureLabel)) &&
      payload_writer.WriteUInt32(chlo_hash.size()) && payload_writer.WriteStringPiece(chlo_hash) &&
      payload_writer.WriteStringPiece(server_config);
  if (!success) {
    quic::QuicCryptoProof proof;
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }

  // TODO(danzh) Get the signature algorithm from leaf cert.
  auto signature_callback = std::make_unique<SignatureCallback>(std::move(callback), chain);
  ComputeTlsSignature(server_address, client_address, hostname, SSL_SIGN_RSA_PSS_RSAE_SHA256,
                      quiche::QuicheStringPiece(payload.get(), payload_size),
                      std::move(signature_callback));
}

} // namespace Quic
} // namespace Envoy
