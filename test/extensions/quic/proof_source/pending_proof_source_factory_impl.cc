#include "test/extensions/quic/proof_source/pending_proof_source_factory_impl.h"

#include "source/common/quic/envoy_quic_proof_source.h"

namespace Envoy {
namespace Quic {

class PendingProofSource : public EnvoyQuicProofSource {
public:
  PendingProofSource(Network::Socket& listen_socket,
                     Network::FilterChainManager& filter_chain_manager,
                     Server::ListenerStats& listener_stats, TimeSource& time_source)
      : EnvoyQuicProofSource(listen_socket, filter_chain_manager, listener_stats, time_source) {}

protected:
  void signPayload(const quic::QuicSocketAddress& /*server_address*/,
                   const quic::QuicSocketAddress& /*client_address*/,
                   const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
                   absl::string_view /*in*/,
                   std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override {
    // Make the callback pending.
    pending_callbacks_.push_back(std::move(callback));
  }

private:
  std::vector<std::unique_ptr<quic::ProofSource::SignatureCallback>> pending_callbacks_;
};

std::unique_ptr<quic::ProofSource> PendingProofSourceFactoryImpl::createQuicProofSource(
    Network::Socket& listen_socket, Network::FilterChainManager& filter_chain_manager,
    Server::ListenerStats& listener_stats, TimeSource& time_source) {
  return std::make_unique<PendingProofSource>(listen_socket, filter_chain_manager, listener_stats,
                                              time_source);
}

REGISTER_FACTORY(PendingProofSourceFactoryImpl, EnvoyQuicProofSourceFactoryInterface);

} // namespace Quic
} // namespace Envoy
