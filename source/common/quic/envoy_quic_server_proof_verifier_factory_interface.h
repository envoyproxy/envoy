#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "quiche/quic/core/crypto/proof_verifier.h"

namespace Envoy {
namespace Quic {

// A factory interface to provide quic::ProofVerifier for server-side client certificate validation.
// This factory creates ProofVerifier instances that enforce client certificate requirements
// during QUIC handshakes.
class EnvoyQuicServerProofVerifierFactoryInterface : public Config::TypedFactory {
public:
  ~EnvoyQuicServerProofVerifierFactoryInterface() override = default;

  std::string category() const override { return "envoy.quic.server.proof_verifier"; }

  virtual std::unique_ptr<quic::ProofVerifier>
  createQuicServerProofVerifier(Network::Socket& listen_socket,
                                Network::FilterChainManager& filter_chain_manager,
                                TimeSource& time_source) PURE;
};

} // namespace Quic
} // namespace Envoy
