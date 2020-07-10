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

// A ProofSource::Detail implementation which retains filter chain.
class EnvoyQuicProofSourceDetails : public quic::ProofSource::Details {
public:
  explicit EnvoyQuicProofSourceDetails(const Network::FilterChain& filter_chain)
      : filter_chain_(filter_chain) {}
  EnvoyQuicProofSourceDetails(const EnvoyQuicProofSourceDetails& other)
      : filter_chain_(other.filter_chain_) {}

  const Network::FilterChain& filterChain() const { return filter_chain_; }

private:
  const Network::FilterChain& filter_chain_;
};

// A fake implementation of quic::ProofSource which uses RSA cipher suite to sign in GetProof().
// TODO(danzh) Rename it to EnvoyQuicProofSource once it's fully implemented.
class EnvoyQuicFakeProofSource : public quic::ProofSource {
public:
  ~EnvoyQuicFakeProofSource() override = default;

  // quic::ProofSource
  // Returns a certs chain and its fake SCT "Fake timestamp" and TLS signature wrapped
  // in QuicCryptoProof.
  void GetProof(const quic::QuicSocketAddress& server_address,
                const quic::QuicSocketAddress& client_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion /*transport_version*/,
                quiche::QuicheStringPiece /*chlo_hash*/,
                std::unique_ptr<quic::ProofSource::Callback> callback) override {
    quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain =
        GetCertChain(server_address, client_address, hostname);
    quic::QuicCryptoProof proof;
    // TODO(danzh) Get the signature algorithm from leaf cert.
    auto signature_callback = std::make_unique<SignatureCallback>(std::move(callback), chain);
    ComputeTlsSignature(server_address, client_address, hostname, SSL_SIGN_RSA_PSS_RSAE_SHA256,
                        server_config, std::move(signature_callback));
  }

  TicketCrypter* GetTicketCrypter() override { return nullptr; }

private:
  // Used by GetProof() to get signature.
  class SignatureCallback : public quic::ProofSource::SignatureCallback {
  public:
    // TODO(danzh) Pass in Details to retain the certs chain, and quic::ProofSource::Callback to be
    // triggered in Run().
    SignatureCallback(std::unique_ptr<quic::ProofSource::Callback> callback,
                      quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain)
        : callback_(std::move(callback)), chain_(chain) {}

    // quic::ProofSource::SignatureCallback
    void Run(bool ok, std::string signature, std::unique_ptr<Details> details) override {
      quic::QuicCryptoProof proof;
      if (!ok) {
        callback_->Run(false, chain_, proof, nullptr);
        return;
      }
      proof.signature = signature;
      proof.leaf_cert_scts = "Fake timestamp";
      callback_->Run(true, chain_, proof, std::move(details));
    }

  private:
    std::unique_ptr<quic::ProofSource::Callback> callback_;
    quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> chain_;
  };
};

} // namespace Quic
} // namespace Envoy
