#pragma once

#include <memory>

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/common/quic/quic_ssl_connection_info.h"
#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Quic {

class CertVerifyResult : public quic::ProofVerifyDetails {
public:
  explicit CertVerifyResult(bool is_valid) : is_valid_(is_valid) {}

  ProofVerifyDetails* Clone() const override { return new CertVerifyResult(is_valid_); }

  bool isValid() const { return is_valid_; }

private:
  bool is_valid_{false};
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
                  const std::vector<std::string>& certs, const std::string& ocsp_response,
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
