#include "source/common/quic/envoy_quic_proof_verifier_base.h"

#include "source/common/quic/envoy_quic_utils.h"

#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/quic_data_writer.h"

namespace Envoy {
namespace Quic {

quic::QuicAsyncStatus EnvoyQuicProofVerifierBase::VerifyProof(
    const std::string& /*hostname*/, const uint16_t /*port*/, const std::string& /*server_config*/,
    quic::QuicTransportVersion /*quic_version*/, absl::string_view /*chlo_hash*/,
    const std::vector<std::string>& /*certs*/, const std::string& /*cert_sct*/,
    const std::string& /*signature*/, const quic::ProofVerifyContext* /*context*/,
    std::string* /*error_details*/, std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
    std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {
  // Only reachable in Google QUIC which is not supported by Envoy.
  PANIC("not implemented");
}

} // namespace Quic
} // namespace Envoy
