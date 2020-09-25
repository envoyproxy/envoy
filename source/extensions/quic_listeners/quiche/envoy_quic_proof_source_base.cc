#include "extensions/quic_listeners/quiche/envoy_quic_proof_source_base.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "quiche/quic/core/quic_data_writer.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

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

  if (chain == nullptr || chain->certs.empty()) {
    quic::QuicCryptoProof proof;
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }
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

  std::string error_details;
  bssl::UniquePtr<X509> cert = parseDERCertificate(chain->certs[0], &error_details);
  if (cert == nullptr) {
    ENVOY_LOG(warn, absl::StrCat("Invalid leaf cert: ", error_details));
    quic::QuicCryptoProof proof;
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }

  bssl::UniquePtr<EVP_PKEY> pub_key(X509_get_pubkey(cert.get()));
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pub_key.get(), &error_details);
  if (sign_alg == 0) {
    ENVOY_LOG(warn, absl::StrCat("Failed to deduce signature algorithm from public key: ",
                                 error_details));
    quic::QuicCryptoProof proof;
    callback->Run(/*ok=*/false, nullptr, proof, nullptr);
    return;
  }

  auto signature_callback = std::make_unique<SignatureCallback>(std::move(callback), chain);

  signPayload(server_address, client_address, hostname, sign_alg,
              quiche::QuicheStringPiece(payload.get(), payload_size),
              std::move(signature_callback));
}

void EnvoyQuicProofSourceBase::ComputeTlsSignature(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, quiche::QuicheStringPiece in,
    std::unique_ptr<quic::ProofSource::SignatureCallback> callback) {
  signPayload(server_address, client_address, hostname, signature_algorithm, in,
              std::move(callback));
}

} // namespace Quic
} // namespace Envoy
