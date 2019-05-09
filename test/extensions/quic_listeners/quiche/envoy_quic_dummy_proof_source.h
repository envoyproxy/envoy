#pragma once

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/platform/api/quic_reference_counted.h"

namespace Envoy {

// A dummy implementation of ProofSource interface which provides identical
// dummy CryptoProof, Chain and signature string. This is used for testing
// purpose before we have a real ProofSource which reads certs from Envoy
// config.
class EnvoyQuicDummyProofSource : public quic::ProofSource {
public:
  EnvoyQuicFakeProofSource() {}
  ~EnvoyQuicFakeProofSource() override {}

  // ProofSource
  void GetProof(const quic::QuicSocketAddress& server_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion transport_version,
                quic::QuicStringPiece chlo_hash,
                std::unique_ptr<quic::Callback> callback) override {
    quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain =
        GetCertChain(server_address, hostname);
    quic::QuicCryptoProof proof;
    proof.signature = "Dummy signature";
    proof.leaf_cert_scts = "Dummy timestamp";
    callback->Run(true, chain, proof, nullptr /* details */);
  }

  quic::QuicReferenceCountedPointer<Chain>
  GetCertChain(const quic::QuicSocketAddress& server_address,
               const std::string& hostname) override {
    std::vector<std::string> certs;
    certs.push_back(kDummyCertName);
    return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(
        new quic::ProofSource::Chain(certs));
  }

  void ComputeTlsSignature(const quic::QuicSocketAddress& server_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           quic::QuicStringPiece in,
                           std::unique_ptr<quic::SignatureCallback> callback) override {
    callback->Run(true, "Dummy signature");
  }
};

} // namespace Envoy
