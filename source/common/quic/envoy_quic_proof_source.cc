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
  CertWithFilterChain res =
      getTlsCertConfigAndFilterChain(server_address, client_address, hostname, cert_matched_sni);
  return res.cert_;
}

void EnvoyQuicProofSource::signPayload(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, absl::string_view in,
    std::unique_ptr<quic::ProofSource::SignatureCallback> callback) {
  CertWithFilterChain res = getTlsCertConfigAndFilterChain(server_address, client_address, hostname,
                                                           nullptr /* cert_matched_sni */);
  if (res.private_key_ == nullptr) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    callback->Run(false, "", nullptr);
    return;
  }

  // Verify the signature algorithm is as expected.
  std::string error_details;
  int sign_alg =
      deduceSignatureAlgorithmFromPublicKey(res.private_key_->private_key(), &error_details);
  if (sign_alg != signature_algorithm) {
    ENVOY_LOG(warn,
              fmt::format("The signature algorithm {} from the private key is not expected: {}",
                          sign_alg, error_details));
    callback->Run(false, "", nullptr);
    return;
  }

  // Sign.
  std::string sig = res.private_key_->Sign(in, signature_algorithm);
  bool success = !sig.empty();
  ASSERT(res.filter_chain_.has_value());
  callback->Run(success, sig,
                std::make_unique<EnvoyQuicProofSourceDetails>(res.filter_chain_.value().get()));
}

EnvoyQuicProofSource::CertWithFilterChain EnvoyQuicProofSource::getTlsCertConfigAndFilterChain(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, bool* cert_matched_sni) {
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
    return {};
  }
  ENVOY_LOG(trace, "Got a matching cert chain {}", filter_chain->name());

  auto& transport_socket_factory =
      dynamic_cast<const QuicServerTransportSocketFactory&>(filter_chain->transportSocketFactory());

  auto [cert, key] = transport_socket_factory.getTlsCertificateAndKey(hostname, cert_matched_sni);
  if (cert == nullptr || key == nullptr) {
    ENVOY_LOG(warn, "No certificate is configured in transport socket config.");
    return {};
  }
  return {std::move(cert), std::move(key), *filter_chain};
}

void EnvoyQuicProofSource::updateFilterChainManager(
    Network::FilterChainManager& filter_chain_manager) {
  filter_chain_manager_ = &filter_chain_manager;
}

} // namespace Quic
} // namespace Envoy
