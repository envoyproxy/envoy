#pragma once

#include "source/common/quic/envoy_quic_proof_source_base.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/server/listener_stats.h"

namespace Envoy {
namespace Quic {

// A ProofSource implementation which supplies a proof instance with certs from filter chain.
class EnvoyQuicProofSource : public EnvoyQuicProofSourceBase {
public:
  EnvoyQuicProofSource(Network::Socket& listen_socket,
                       Network::FilterChainManager& filter_chain_manager,
                       Server::ListenerStats& listener_stats, TimeSource& time_source)
      : listen_socket_(listen_socket), filter_chain_manager_(&filter_chain_manager),
        listener_stats_(listener_stats), time_source_(time_source) {}

  ~EnvoyQuicProofSource() override = default;

  // quic::ProofSource
  void OnNewSslCtx(SSL_CTX* ssl_ctx) override;
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& server_address,
               const quic::QuicSocketAddress& client_address, const std::string& hostname,
               bool* cert_matched_sni) override;

  void updateFilterChainManager(Network::FilterChainManager& filter_chain_manager);

  // Returns the SSL ex_data index used to store filter chain pointer during QUIC handshakes.
  static int filterChainExDataIndex();

  // Session ticket key callback installed on SSL_CTX by OnNewSslCtx.
  // Looks up the filter chain from SSL ex_data and delegates to the
  // transport socket factory's processSessionTicket.
  static int ticketKeyCallback(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                               HMAC_CTX* hmac_ctx, int encrypt);

  struct TransportSocketFactoryWithFilterChain {
    const QuicServerTransportSocketFactory& transport_socket_factory_;
    const Network::FilterChain& filter_chain_;
  };

  absl::optional<TransportSocketFactoryWithFilterChain>
  getTransportSocketAndFilterChain(const quic::QuicSocketAddress& server_address,
                                   const quic::QuicSocketAddress& client_address,
                                   const std::string& hostname);

protected:
  // quic::ProofSource
  void signPayload(const quic::QuicSocketAddress& server_address,
                   const quic::QuicSocketAddress& client_address, const std::string& hostname,
                   uint16_t signature_algorithm, absl::string_view in,
                   std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override;

private:
  struct CertWithFilterChain {
    quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> cert_;
    std::shared_ptr<quic::CertificatePrivateKey> private_key_;
    absl::optional<std::reference_wrapper<const Network::FilterChain>> filter_chain_;
  };

  CertWithFilterChain getTlsCertAndFilterChain(const TransportSocketFactoryWithFilterChain& data,
                                               const std::string& hostname, bool* cert_matched_sni);

  Network::Socket& listen_socket_;
  Network::FilterChainManager* filter_chain_manager_{nullptr};
  Server::ListenerStats& listener_stats_;
  TimeSource& time_source_;
};

} // namespace Quic
} // namespace Envoy
