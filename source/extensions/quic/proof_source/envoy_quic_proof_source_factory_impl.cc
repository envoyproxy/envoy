#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::ProofSource> EnvoyQuicProofSourceFactoryImpl::createQuicProofSource(
    Network::Socket& listen_socket, Network::FilterChainManager& filter_chain_manager,
    Server::ListenerStats& listener_stats, TimeSource& time_source) {
  return std::make_unique<EnvoyQuicProofSource>(listen_socket, filter_chain_manager, listener_stats,
                                                time_source);
}

REGISTER_FACTORY(EnvoyQuicProofSourceFactoryImpl, EnvoyQuicProofSourceFactoryInterface);

} // namespace Quic
} // namespace Envoy
