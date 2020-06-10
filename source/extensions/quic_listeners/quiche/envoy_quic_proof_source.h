#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicProofSource : public EnvoyQuicFakeProofSource,
                             protected Logger::Loggable<Logger::Id::quic> {
public:
  EnvoyQuicProofSource(Network::SocketSharedPtr listen_socket,
                       Network::FilterChainManager& filter_chain_manager)
      : listen_socket_(std::move(listen_socket)), filter_chain_manager_(filter_chain_manager) {}

  ~EnvoyQuicProofSource() override = default;

  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& server_address,
               const quic::QuicSocketAddress& client_address, const std::string& hostname) override;
  void ComputeTlsSignature(const quic::QuicSocketAddress& server_address,
                           const quic::QuicSocketAddress& client_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           quiche::QuicheStringPiece in,
                           std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override;

private:
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  getTlsCertConfig(const quic::QuicSocketAddress& server_address,
                   const quic::QuicSocketAddress& client_address, const std::string& hostname);

  Network::SocketSharedPtr listen_socket_;
  Network::FilterChainManager& filter_chain_manager_;
};

} // namespace Quic
} // namespace Envoy
