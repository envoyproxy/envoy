#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"

#include <openssl/bio.h>

#include "envoy/ssl/tls_certificate_config.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"
#include "extensions/transport_sockets/well_known_names.h"

#include "openssl/bytestring.h"
#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
EnvoyQuicProofSource::GetCertChain(const quic::QuicSocketAddress& server_address,
                                   const quic::QuicSocketAddress& client_address,
                                   const std::string& hostname) {
  CertConfigWithFilterChain res =
      getTlsCertConfigAndFilterChain(server_address, client_address, hostname);
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_ref =
      res.cert_config_;
  if (!cert_config_ref.has_value()) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    return nullptr;
  }
  auto& cert_config = cert_config_ref.value().get();
  const std::string& chain_str = cert_config.certificateChain();
  std::stringstream pem_stream(chain_str);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(
      new quic::ProofSource::Chain(chain));
}

void EnvoyQuicProofSource::signPayload(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, quiche::QuicheStringPiece in,
    std::unique_ptr<quic::ProofSource::SignatureCallback> callback) {
  CertConfigWithFilterChain res =
      getTlsCertConfigAndFilterChain(server_address, client_address, hostname);
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_ref =
      res.cert_config_;
  if (!cert_config_ref.has_value()) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    callback->Run(false, "", nullptr);
    return;
  }
  auto& cert_config = cert_config_ref.value().get();
  // Load private key.
  const std::string& pkey = cert_config.privateKey();
  std::stringstream pem_str(pkey);
  std::unique_ptr<quic::CertificatePrivateKey> pem_key =
      quic::CertificatePrivateKey::LoadPemFromStream(&pem_str);
  if (pem_key == nullptr) {
    ENVOY_LOG(warn, "Failed to load private key.");
    callback->Run(false, "", nullptr);
    return;
  }
  // Verify the signature algorithm is as expected.
  std::string error_details;
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pem_key->private_key(), &error_details);
  if (sign_alg != signature_algorithm) {
    ENVOY_LOG(warn,
              fmt::format("The signature algorithm {} from the private key is not expected: {}",
                          sign_alg, error_details));
    callback->Run(false, "", nullptr);
    return;
  }

  // Sign.
  std::string sig = pem_key->Sign(in, signature_algorithm);
  bool success = !sig.empty();
  ASSERT(res.filter_chain_.has_value());
  callback->Run(success, sig,
                std::make_unique<EnvoyQuicProofSourceDetails>(res.filter_chain_.value().get()));
}

EnvoyQuicProofSource::CertConfigWithFilterChain
EnvoyQuicProofSource::getTlsCertConfigAndFilterChain(const quic::QuicSocketAddress& server_address,
                                                     const quic::QuicSocketAddress& client_address,
                                                     const std::string& hostname) {
  ENVOY_LOG(trace, "Getting cert chain for {}", hostname);
  Network::ConnectionSocketImpl connection_socket(
      std::make_unique<QuicIoHandleWrapper>(listen_socket_.ioHandle()),
      quicAddressToEnvoyAddressInstance(server_address),
      quicAddressToEnvoyAddressInstance(client_address));
  connection_socket.setDetectedTransportProtocol(
      Extensions::TransportSockets::TransportProtocolNames::get().Quic);
  connection_socket.setRequestedServerName(hostname);
  connection_socket.setRequestedApplicationProtocols({"h2"});
  const Network::FilterChain* filter_chain =
      filter_chain_manager_.findFilterChain(connection_socket);
  if (filter_chain == nullptr) {
    listener_stats_.no_filter_chain_match_.inc();
    return {absl::nullopt, absl::nullopt};
  }
  const Network::TransportSocketFactory& transport_socket_factory =
      filter_chain->transportSocketFactory();
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs =
      dynamic_cast<const QuicServerTransportSocketFactory&>(transport_socket_factory)
          .serverContextConfig()
          .tlsCertificates();

  // Only return the first TLS cert config.
  // TODO(danzh) Choose based on supported cipher suites in TLS1.3 CHLO and prefer EC
  // certs if supported.
  return {tls_cert_configs[0].get(), *filter_chain};
}

} // namespace Quic
} // namespace Envoy
