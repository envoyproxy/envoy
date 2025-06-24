#include "source/extensions/quic/proof_verifier/envoy_quic_server_proof_verifier_factory_impl.h"

#include "source/common/quic/envoy_quic_server_proof_verifier.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::ProofVerifier>
EnvoyQuicServerProofVerifierFactoryImpl::createQuicServerProofVerifier(
    Network::Socket& listen_socket, Network::FilterChainManager& filter_chain_manager,
    TimeSource& time_source) {
  return std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket, filter_chain_manager,
                                                        time_source);
}

REGISTER_FACTORY(EnvoyQuicServerProofVerifierFactoryImpl,
                 EnvoyQuicServerProofVerifierFactoryInterface);

} // namespace Quic
} // namespace Envoy
