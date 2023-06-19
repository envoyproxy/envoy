#include "source/common/quic/envoy_quic_proof_source.h"

#include <openssl/bio.h>

#include "envoy/ssl/tls_certificate_config.h"

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_io_handle_wrapper.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "openssl/bytestring.h"
#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>
EnvoyQuicProofSource::GetCertChain(const quic::QuicSocketAddress& server_address,
                                   const quic::QuicSocketAddress& client_address,
                                   const std::string& hostname, bool* cert_matched_sni) {
  // TODO(DavidSchinazi) parse the certificate to correctly fill in |cert_matched_sni|.
  *cert_matched_sni = false;

  CertConfigWithFilterChain res =
      getTlsCertConfigAndFilterChain(server_address, client_address, hostname);
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_ref =
      res.cert_config_;
  if (!cert_config_ref.has_value()) {
    return nullptr;
  }
  auto& cert_config = cert_config_ref.value().get();
  const std::string& chain_str = cert_config.certificateChain();
  std::stringstream pem_stream(chain_str);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);

  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> cert_chain(
      new quic::ProofSource::Chain(chain));
  std::string error_details;
  bssl::UniquePtr<X509> cert = parseDERCertificate(cert_chain->certs[0], &error_details);
  if (cert == nullptr) {
    ENVOY_LOG(warn, absl::StrCat("Invalid leaf cert: ", error_details));
    return nullptr;
  }

  bssl::UniquePtr<EVP_PKEY> pub_key(X509_get_pubkey(cert.get()));
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pub_key.get(), &error_details);
  if (sign_alg == 0) {
    ENVOY_LOG(warn, absl::StrCat("Failed to deduce signature algorithm from public key: ",
                                 error_details));
    return nullptr;
  }
  return cert_chain;
}

void EnvoyQuicProofSource::signPayload(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, absl::string_view in,
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
  // TODO(danzh) modify QUICHE to make quic session or ALPN accessible to avoid hard-coded ALPN.
  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), server_address, client_address, hostname, "h3");
  StreamInfo::StreamInfoImpl info(time_source_,
                                  connection_socket->connectionInfoProviderSharedPtr());
  const Network::FilterChain* filter_chain =
      filter_chain_manager_->findFilterChain(*connection_socket, info);

  if (filter_chain == nullptr) {
    listener_stats_.no_filter_chain_match_.inc();
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    return {absl::nullopt, absl::nullopt};
  }
  ENVOY_LOG(trace, "Got a matching cert chain {}", filter_chain->name());

  auto& transport_socket_factory =
      dynamic_cast<const QuicServerTransportSocketFactory&>(filter_chain->transportSocketFactory());

  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs =
      transport_socket_factory.getTlsCertificates();
  if (tls_cert_configs.empty()) {
    ENVOY_LOG(warn, "No certificate is configured in transport socket config.");
    return {absl::nullopt, absl::nullopt};
  }
  // Only return the first TLS cert config.
  // TODO(danzh) Choose based on supported cipher suites in TLS1.3 CHLO and prefer EC
  // certs if supported.
  return {tls_cert_configs[0].get(), *filter_chain};
}

void EnvoyQuicProofSource::updateFilterChainManager(
    Network::FilterChainManager& filter_chain_manager) {
  filter_chain_manager_ = &filter_chain_manager;
}

} // namespace Quic
} // namespace Envoy
