#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "source/common/quic/envoy_quic_proof_verifier_base.h"

namespace Envoy {
namespace Quic {

// Server-side QUIC proof verifier that validates client certificates during the
// TLS handshake when the matched filter chain requires a client certificate.
class EnvoyQuicServerProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  EnvoyQuicServerProofVerifier(Network::Socket& listen_socket,
                               Network::FilterChainManager& filter_chain_manager,
                               TimeSource& time_source);

  ~EnvoyQuicServerProofVerifier() override = default;

  // quic::ProofVerifier
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<absl::string_view>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

  void updateFilterChainManager(Network::FilterChainManager& filter_chain_manager);

private:
  Network::Socket& listen_socket_;
  Network::FilterChainManager* filter_chain_manager_;
  TimeSource& time_source_;
};

} // namespace Quic
} // namespace Envoy
