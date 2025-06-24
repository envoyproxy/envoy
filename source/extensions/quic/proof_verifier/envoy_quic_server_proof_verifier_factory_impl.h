#pragma once

#include "envoy/extensions/quic/proof_source/v3/proof_source.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_server_proof_verifier.h"
#include "source/common/quic/envoy_quic_server_proof_verifier_factory_interface.h"

namespace Envoy {
namespace Quic {

// Provides a ProofVerifier implementation for server-side client certificate validation during QUIC
// handshakes.
class EnvoyQuicServerProofVerifierFactoryImpl
    : public EnvoyQuicServerProofVerifierFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::proof_source::v3::ProofSourceConfig>();
  }

  std::string name() const override { return "envoy.quic.server.proof_verifier.filter_chain"; }

  std::unique_ptr<quic::ProofVerifier>
  createQuicServerProofVerifier(Network::Socket& listen_socket,
                                Network::FilterChainManager& filter_chain_manager,
                                TimeSource& time_source) override;
};

DECLARE_FACTORY(EnvoyQuicServerProofVerifierFactoryImpl);

} // namespace Quic
} // namespace Envoy
