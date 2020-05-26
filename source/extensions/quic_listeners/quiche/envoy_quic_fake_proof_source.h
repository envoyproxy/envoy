#pragma once

#include <string>

#include "common/common/assert.h"

#include "absl/strings/str_cat.h"

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_versions.h"

#pragma GCC diagnostic pop

#include "openssl/ssl.h"
#include "envoy/network/filter.h"
#include "quiche/quic/platform/api/quic_reference_counted.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_string_piece.h"

namespace Envoy {
namespace Quic {

// A fake implementation of quic::ProofSource which returns a fake cert and
// a fake signature for a given QUIC server config.
class EnvoyQuicFakeProofSource : public quic::ProofSource {
public:
  ~EnvoyQuicFakeProofSource() override = default;

  // quic::ProofSource
  // Returns a fake certs chain and its fake SCT "Fake timestamp" and fake TLS signature wrapped
  // in QuicCryptoProof.
  void GetProof(const quic::QuicSocketAddress& server_address,
                const quic::QuicSocketAddress& client_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion /*transport_version*/,
                quiche::QuicheStringPiece /*chlo_hash*/,
                std::unique_ptr<quic::ProofSource::Callback> callback) override {
    quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain =
        GetCertChain(server_address, client_address, hostname);
    quic::QuicCryptoProof proof;
    bool success = false;
    auto signature_callback = std::make_unique<FakeSignatureCallback>(success, proof.signature);
    ComputeTlsSignature(server_address, client_address, hostname, SSL_SIGN_RSA_PSS_RSAE_SHA256,
                        server_config, std::move(signature_callback));
    ASSERT(success);
    proof.leaf_cert_scts = "Fake timestamp";
    callback->Run(true, chain, proof, nullptr /* details */);
  }

  TicketCrypter* GetTicketCrypter() override { return nullptr; }

private:
  // Used by GetProof() to get fake signature.
  class FakeSignatureCallback : public quic::ProofSource::SignatureCallback {
  public:
    FakeSignatureCallback(bool& success, std::string& signature)
        : success_(success), signature_(signature) {}

    // quic::ProofSource::SignatureCallback
    void Run(bool ok, std::string signature, std::unique_ptr<Details> /*details*/) override {
      success_ = ok;
      signature_ = signature;
    }

  private:
    bool& success_;
    std::string& signature_;
  };
};

} // namespace Quic
} // namespace Envoy
