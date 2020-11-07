#pragma once

#include <string>

#include "common/common/assert.h"

#include "absl/strings/str_cat.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/platform/api/quic_reference_counted.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_string_piece.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "openssl/ssl.h"
#include "envoy/network/filter.h"
#include "server/backtrace.h"
#include "common/common/logger.h"

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

// A partial implementation of quic::ProofSource which chooses a cipher suite according to the leaf
// cert to sign in GetProof().
class EnvoyQuicProofSourceBase : public quic::ProofSource,
                                 protected Logger::Loggable<Logger::Id::quic> {
public:
  ~EnvoyQuicProofSourceBase() override = default;

  // quic::ProofSource
  // Returns a certs chain and its fake SCT "Fake timestamp" and TLS signature wrapped
  // in QuicCryptoProof.
  void GetProof(const quic::QuicSocketAddress& server_address,
                const quic::QuicSocketAddress& client_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion /*transport_version*/,
                quiche::QuicheStringPiece chlo_hash,
                std::unique_ptr<quic::ProofSource::Callback> callback) override;

  TicketCrypter* GetTicketCrypter() override { return nullptr; }

  void ComputeTlsSignature(const quic::QuicSocketAddress& server_address,
                           const quic::QuicSocketAddress& client_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           quiche::QuicheStringPiece in,
                           std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override;

protected:
  virtual void signPayload(const quic::QuicSocketAddress& server_address,
                           const quic::QuicSocketAddress& client_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           quiche::QuicheStringPiece in,
                           std::unique_ptr<quic::ProofSource::SignatureCallback> callback) PURE;

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
