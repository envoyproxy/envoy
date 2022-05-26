#pragma once

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/extensions/transport_sockets/tls/context_impl.h"

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

// An interface for the Envoy specific QUICHE verify context.
class EnvoyQuicProofVerifyContext : public quic::ProofVerifyContext {
public:
  virtual Event::Dispatcher& dispatcher() const PURE;
  virtual const Network::TransportSocketOptionsConstSharedPtr& transportSocketOptions() const PURE;
};

// An implementation of the verify context interface.
class EnvoyQuicProofVerifyContextImpl : public EnvoyQuicProofVerifyContext {
public:
  EnvoyQuicProofVerifyContextImpl(
      Event::Dispatcher& dispatcher,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options)
      : dispatcher_(dispatcher), transport_socket_options_(transport_socket_options) {}

  Event::Dispatcher& dispatcher() const override { return dispatcher_; }
  const Network::TransportSocketOptionsConstSharedPtr& transportSocketOptions() const override {
    return transport_socket_options_;
  }

private:
  Event::Dispatcher& dispatcher_;
  const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options_;
};

// A quic::ProofVerifier implementation which verifies cert chain using SSL
// client context config.
class EnvoyQuicProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  EnvoyQuicProofVerifier(Envoy::Ssl::ClientContextSharedPtr&& context)
      : context_(std::move(context)) {
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
  bool doVerifyCertChain(const std::string& hostname, const uint16_t port,
                         const std::vector<std::string>& certs, const std::string& ocsp_response,
                         const std::string& cert_sct, const quic::ProofVerifyContext* context,
                         std::string* error_details, uint8_t* out_alert,
                         std::unique_ptr<quic::ProofVerifierCallback> callback);

  Envoy::Ssl::ClientContextSharedPtr context_;
};

} // namespace Quic
} // namespace Envoy
