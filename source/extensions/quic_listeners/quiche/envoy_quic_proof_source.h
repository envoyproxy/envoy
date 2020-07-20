#pragma once

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_proof_source_base.h"
#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"

namespace Envoy {
namespace Quic {

// A ProofSource implementation which supplies a proof instance with certs from filter chain.
class EnvoyQuicProofSource : public EnvoyQuicProofSourceBase {
public:
  EnvoyQuicProofSource(Network::Socket& listen_socket,
                       Network::FilterChainManager& filter_chain_manager,
                       Server::ListenerStats& listener_stats)
      : listen_socket_(listen_socket), filter_chain_manager_(filter_chain_manager),
        listener_stats_(listener_stats) {}

  ~EnvoyQuicProofSource() override = default;

  // quic::ProofSource
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& server_address,
               const quic::QuicSocketAddress& client_address, const std::string& hostname) override;

protected:
  // quic::ProofSource
  void signPayload(const quic::QuicSocketAddress& server_address,
                   const quic::QuicSocketAddress& client_address, const std::string& hostname,
                   uint16_t signature_algorithm, quiche::QuicheStringPiece in,
                   std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override;

private:
  struct CertConfigWithFilterChain {
    absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_;
    absl::optional<std::reference_wrapper<const Network::FilterChain>> filter_chain_;
  };

  CertConfigWithFilterChain
  getTlsCertConfigAndFilterChain(const quic::QuicSocketAddress& server_address,
                                 const quic::QuicSocketAddress& client_address,
                                 const std::string& hostname);

  Network::Socket& listen_socket_;
  Network::FilterChainManager& filter_chain_manager_;
  Server::ListenerStats& listener_stats_;
};

} // namespace Quic
} // namespace Envoy
