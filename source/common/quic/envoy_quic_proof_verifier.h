#pragma once

#include <memory>
#include <vector>

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/common/quic/quic_ssl_connection_info.h"
#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Quic {

class CertVerifyResult : public quic::ProofVerifyDetails {
public:
  explicit CertVerifyResult(bool is_valid) : is_valid_(is_valid) {}
  CertVerifyResult(bool is_valid, std::vector<bssl::UniquePtr<X509>> validated_chain)
      : is_valid_(is_valid), validated_chain_(std::move(validated_chain)) {}

  ProofVerifyDetails* Clone() const override {
    std::vector<bssl::UniquePtr<X509>> chain;
    chain.reserve(validated_chain_.size());
    for (const auto& cert : validated_chain_) {
      chain.push_back(bssl::UpRef(cert.get()));
    }
    return new CertVerifyResult(is_valid_, std::move(chain));
  }

  bool isValid() const { return is_valid_; }

  // The certificate chain built during verification (leaf first, issuers following), as opposed to
  // the unverified list of certificates sent by the peer. Empty if validation did not build a
  // chain (e.g. SSL_VERIFY_NONE or asynchronous validation).
  const std::vector<bssl::UniquePtr<X509>>& validatedChain() const { return validated_chain_; }

private:
  bool is_valid_{false};
  std::vector<bssl::UniquePtr<X509>> validated_chain_;
};

using CertVerifyResultPtr = std::unique_ptr<CertVerifyResult>();

// An interface for the Envoy specific QUIC verify context.
class EnvoyQuicProofVerifyContext : public quic::ProofVerifyContext {
public:
  virtual Event::Dispatcher& dispatcher() const PURE;
  virtual bool isServer() const PURE;
  virtual const Network::TransportSocketOptionsConstSharedPtr& transportSocketOptions() const PURE;
  virtual Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext
  extraValidationContext() const PURE;
};

using EnvoyQuicProofVerifyContextPtr = std::unique_ptr<EnvoyQuicProofVerifyContext>;

// A quic::ProofVerifier implementation which verifies cert chain using SSL
// client context config.
class EnvoyQuicProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  explicit EnvoyQuicProofVerifier(Envoy::Ssl::ClientContextSharedPtr&& context,
                                  bool accept_untrusted = false)
      : context_(std::move(context)), accept_untrusted_(accept_untrusted) {
    ASSERT(context_.get());
  }

  // EnvoyQuicProofVerifierBase
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<absl::string_view>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

private:
  Envoy::Ssl::ClientContextSharedPtr context_;
  // True if the verifier should accept untrusted certs (see documentation for
  // envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::ACCEPT_UNTRUSTED)
  bool accept_untrusted_;
};

} // namespace Quic
} // namespace Envoy
