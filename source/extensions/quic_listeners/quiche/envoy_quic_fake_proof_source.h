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

#include "quiche/quic/platform/api/quic_reference_counted.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

namespace Envoy {
namespace Quic {

static constexpr char dummy_cert_prefix[] = "Dummy cert from ";

// A dummy implementation of quic::ProofSource which returns a dummy cert and
// a fake signature for given QUIC server config.
class EnvoyQuicFakeProofSource : public quic::ProofSource {
public:
  class FakeSignatureCallback : public quic::ProofSource::SignatureCallback {
  public:
    FakeSignatureCallback(bool& success, std::string& signature)
        : success_(success), signature_(signature) {}

    // quic::ProofSource::SignatureCallback
    void Run(bool ok, std::string signature) override {
      success_ = ok;
      signature_ = signature;
    }

  private:
    bool& success_;
    std::string& signature_;
  };

  EnvoyQuicFakeProofSource() {}
  ~EnvoyQuicFakeProofSource() override {}

  // quic::ProofSource
  // Returns a fake certs chain and its dummy SCT "Dummy timestamp" and dummy TLS signature wrapped
  // in QuicCryptoProof.
  void GetProof(const quic::QuicSocketAddress& server_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion /*transport_version*/,
                quic::QuicStringPiece /*chlo_hash*/,
                std::unique_ptr<quic::ProofSource::Callback> callback) override {
    quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain =
        GetCertChain(server_address, hostname);
    quic::QuicCryptoProof proof;
    bool success = false;
    auto signature_callback = std::make_unique<FakeSignatureCallback>(success, proof.signature);
    ComputeTlsSignature(server_address, hostname, 0, server_config, std::move(signature_callback));
    ASSERT(success);
    proof.leaf_cert_scts = "Dummy timestamp";
    callback->Run(true, chain, proof, nullptr /* details */);
  }

  // Returns a certs chain with a dummy certificate "Dummy cert from [host_name]".
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& /*server_address*/,
               const std::string& hostname) override {
    std::vector<std::string> certs;
    certs.push_back(absl::StrCat(dummy_cert_prefix, hostname));
    return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(
        new quic::ProofSource::Chain(certs));
  }

  // Always call callback with a signature "Dummy signature for { [server_config] }".
  void
  ComputeTlsSignature(const quic::QuicSocketAddress& /*server_address*/,
                      const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
                      quic::QuicStringPiece in,
                      std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override {
    callback->Run(true, absl::StrCat("Dummy signature for { ", in, " }"));
  }
};

} // namespace Quic
} // namespace Envoy
