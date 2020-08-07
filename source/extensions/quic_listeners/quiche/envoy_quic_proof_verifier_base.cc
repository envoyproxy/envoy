#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier_base.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/quic_data_writer.h"

namespace Envoy {
namespace Quic {

quic::QuicAsyncStatus EnvoyQuicProofVerifierBase::VerifyProof(
    const std::string& hostname, const uint16_t port, const std::string& server_config,
    quic::QuicTransportVersion /*quic_version*/, absl::string_view chlo_hash,
    const std::vector<std::string>& certs, const std::string& cert_sct,
    const std::string& signature, const quic::ProofVerifyContext* context,
    std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
    std::unique_ptr<quic::ProofVerifierCallback> callback) {
  if (certs.empty()) {
    *error_details = "Received empty cert chain.";
    return quic::QUIC_FAILURE;
  }
  if (!verifySignature(server_config, chlo_hash, certs[0], signature, error_details)) {
    return quic::QUIC_FAILURE;
  }

  return VerifyCertChain(hostname, port, certs, "", cert_sct, context, error_details, details,
                         std::move(callback));
}

bool EnvoyQuicProofVerifierBase::verifySignature(const std::string& server_config,
                                                 absl::string_view chlo_hash,
                                                 const std::string& cert,
                                                 const std::string& signature,
                                                 std::string* error_details) {
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(cert);
  if (cert_view == nullptr) {
    *error_details = "Invalid leaf cert.";
    return false;
  }
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), error_details);
  if (sign_alg == 0) {
    return false;
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
    *error_details = "QuicPacketWriter error.";
    return false;
  }
  bool valid = cert_view->VerifySignature(quiche::QuicheStringPiece(payload.get(), payload_size),
                                          signature, sign_alg);
  if (!valid) {
    *error_details = "Signature is not valid.";
  }
  return valid;
}

} // namespace Quic
} // namespace Envoy
