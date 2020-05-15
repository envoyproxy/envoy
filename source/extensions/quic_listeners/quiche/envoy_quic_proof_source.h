#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicProofSource : public EnvoyQuicFakeProofSource,
                            protected Logger::Loggable<Logger::Id::quic> {
public:
  EnvoyQuicProofSource(Network::SocketSharedPtr& listen_socket,
                           Network::FilterChainManager& filter_chain_manager)
      : listen_socket_(listen_socket), filter_chain_manager_(filter_chain_manager) {}

  ~EnvoyQuicProofSource() override = default;

  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& server_address,
               const quic::QuicSocketAddress& client_address,
               const std::string& hostname) override;

private:
  Network::SocketSharedPtr listen_socket_;
  Network::FilterChainManager& filter_chain_manager_;
};

} // namespace Quic
} // namespace Envoy
