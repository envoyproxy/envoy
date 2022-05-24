#pragma once

#include <netinet/tcp.h>

#include <memory>

#include "source/common/quic/envoy_quic_proof_verifier_base.h"
#include "source/common/quic/quic_ssl_connection_info.h"
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

using CertVerifyResultPtr = std::unique_ptr<CertVerifyResult>();

// An interface for the Envoy specific QUICHE verify context.
class EnvoyQuicProofVerifyContext : public quic::ProofVerifyContext {
public:
  virtual absl::string_view getEchNameOverrride() const PURE;
  virtual Event::Dispatcher& dispatcher() PURE;
  virtual bool isServer() const PURE;
};

// An implementation of the verify context interface.
class EnvoyQuicProofVerifyContextImpl : public EnvoyQuicProofVerifyContext {
public:
  EnvoyQuicProofVerifyContextImpl(QuicSslConnectionInfo& ssl_info, Event::Dispatcher& dispatcher,
                                  bool is_server)
      : ssl_info_(ssl_info), dispatcher_(dispatcher), is_server_(is_server) {}

  absl::string_view getEchNameOverrride() const override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  bool isServer() const override { return is_server_; }

private:
  QuicSslConnectionInfo& ssl_info_;
  Event::Dispatcher& dispatcher_;
  const bool is_server_;
};

using EnvoyQuicProofVerifyContextPtr = std::unique_ptr<EnvoyQuicProofVerifyContext>;

// A quic::ProofVerifier implementation which verifies cert chain using SSL
// client context config.
class EnvoyQuicProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  explicit EnvoyQuicProofVerifier(Envoy::Ssl::ClientContextSharedPtr&& context)
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
  // TODO(danzh) remove when deprecating envoy.reloadable_features.tls_aync_cert_validation.
  bool doVerifyCertChain(const std::string& hostname, const uint16_t port,
                         const std::vector<std::string>& certs, const std::string& ocsp_response,
                         const std::string& cert_sct, const quic::ProofVerifyContext* context,
                         std::string* error_details, uint8_t* out_alert,
                         std::unique_ptr<quic::ProofVerifierCallback> callback);

  Envoy::Ssl::ClientContextSharedPtr context_;
};

} // namespace Quic
} // namespace Envoy
