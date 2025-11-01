#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "source/common/quic/envoy_quic_proof_verifier_base.h"

namespace Envoy {
namespace Quic {

// A ProofVerifier implementation for server-side client certificate validation during QUIC
// handshake. This verifier enforces client certificate requirements by validating certificates
// during the initial handshake and rejecting connections that don't meet the requirements.
class EnvoyQuicServerProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  EnvoyQuicServerProofVerifier(Network::Socket& listen_socket,
                               Network::FilterChainManager& filter_chain_manager,
                               TimeSource& time_source);

  ~EnvoyQuicServerProofVerifier() override = default;

  // quic::ProofVerifier
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& hostname, const uint16_t port,
                  const std::vector<std::string>& certs, const std::string& ocsp_response,
                  const std::string& cert_sct, const quic::ProofVerifyContext* context,
                  std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
                  uint8_t* out_alert,
                  std::unique_ptr<quic::ProofVerifierCallback> callback) override;

  void updateFilterChainManager(Network::FilterChainManager& filter_chain_manager);

private:
  // Get the appropriate server context for client certificate validation based on connection
  // details.
  Ssl::ServerContextSharedPtr
  getServerContextForClientValidation(const quiche::QuicheSocketAddress& server_address,
                                      const quiche::QuicheSocketAddress& client_address,
                                      const std::string& hostname);

  // Check if the server context requires client certificates for this connection.
  bool requiresClientCertificateValidation(const quiche::QuicheSocketAddress& server_address,
                                           const quiche::QuicheSocketAddress& client_address,
                                           const std::string& hostname);

  Network::Socket& listen_socket_;
  Network::FilterChainManager* filter_chain_manager_;
  TimeSource& time_source_;
};

} // namespace Quic
} // namespace Envoy
