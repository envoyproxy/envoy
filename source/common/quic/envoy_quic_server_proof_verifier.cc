#include "source/common/quic/envoy_quic_server_proof_verifier.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tls/server_context_impl.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerProofVerifier::EnvoyQuicServerProofVerifier(
    Network::Socket& listen_socket, Network::FilterChainManager& filter_chain_manager,
    TimeSource& time_source)
    : listen_socket_(listen_socket), filter_chain_manager_(&filter_chain_manager),
      time_source_(time_source) {}

quic::QuicAsyncStatus EnvoyQuicServerProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/, const std::vector<std::string>& certs,
    const std::string& /*ocsp_response*/, const std::string& /*cert_sct*/,
    const quic::ProofVerifyContext* /*context*/, std::string* error_details,
    std::unique_ptr<quic::ProofVerifyDetails>* details, uint8_t* /*out_alert*/,
    std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {

  ASSERT(details != nullptr);

  if (!filter_chain_manager_) {
    *error_details = "Filter chain manager not available";
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  auto local_addr = listen_socket_.connectionInfoProvider().localAddress();
  if (!local_addr) {
    *error_details = "No local address available";
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  // Use localhost as peer address for filter chain matching during handshake.
  // The actual client address may not be fully established during the handshake.
  auto localhost_addr = Network::Utility::getCanonicalIpv4LoopbackAddress();
  auto server_quic_addr = envoyIpAddressToQuicSocketAddress(local_addr->ip());
  auto client_quic_addr = envoyIpAddressToQuicSocketAddress(localhost_addr->ip());

  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), server_quic_addr, client_quic_addr, hostname, "h3");

  if (!connection_socket) {
    ENVOY_LOG(debug, "Failed to create connection socket for certificate validation");
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  StreamInfo::StreamInfoImpl stream_info(time_source_,
                                         connection_socket->connectionInfoProviderSharedPtr(),
                                         StreamInfo::FilterState::LifeSpan::Connection);

  const Network::FilterChain* filter_chain =
      filter_chain_manager_->findFilterChain(*connection_socket, stream_info);

  if (!filter_chain) {
    ENVOY_LOG(debug, "No filter chain found for certificate validation");
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  const auto* quic_transport_socket_factory = dynamic_cast<const QuicServerTransportSocketFactory*>(
      &filter_chain->transportSocketFactory());

  if (!quic_transport_socket_factory) {
    ENVOY_LOG(debug, "Transport socket factory is not a QUIC server factory");
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  bool requires_client_cert = quic_transport_socket_factory->requiresClientCertificate();
  ENVOY_LOG(debug, "QUIC server requires client certificate: {}", requires_client_cert);

  if (!requires_client_cert) {
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  // Get the server SSL context for certificate validation.
  auto server_ssl_context = quic_transport_socket_factory->getServerSslContext();
  if (!server_ssl_context) {
    ENVOY_LOG(debug, "No server SSL context available for certificate validation");
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  // Client certificates are required - validate them.
  if (certs.empty()) {
    ENVOY_LOG(warn, "Client certificate required but not provided");
    *error_details = "Client certificate required but not provided";
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  // Parse the certificate chain from DER format.
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (size_t i = 0; i < certs.size(); i++) {
    const auto& cert_der = certs[i];

    bssl::UniquePtr<X509> cert = parseDERCertificate(cert_der, error_details);
    if (!cert) {
      ENVOY_LOG(error, "Failed to parse certificate {}: {}", i,
                error_details ? *error_details : "unknown error");
      return quic::QUIC_FAILURE;
    }

    if (!bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      ENVOY_LOG(error, "Failed to add certificate {} to stack", i);
      *error_details = "Failed to build certificate chain";
      return quic::QUIC_FAILURE;
    }
  }

  // Verify basic certificate chain structure.
  if (sk_X509_num(cert_chain.get()) == 0) {
    ENVOY_LOG(error, "Empty certificate chain when certificates required");
    *error_details = "Empty certificate chain";
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  X509* leaf_cert = sk_X509_value(cert_chain.get(), 0);
  if (!leaf_cert) {
    ENVOY_LOG(error, "Cannot access leaf certificate");
    *error_details = "Cannot access leaf certificate";
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  // Validate the certificate chain against the configured CA.
  auto server_context_impl =
      std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ServerContextImpl>(
          server_ssl_context);
  if (!server_context_impl) {
    ENVOY_LOG(debug, "Could not get server context for certificate validation");
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  // Use the cert validator to verify the certificate chain against the CA.
  Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext validation_context;
  validation_context.callbacks = nullptr;

  Extensions::TransportSockets::Tls::ValidationResults validation_result =
      server_context_impl->customVerifyCertChainForQuic(*cert_chain, nullptr, /*is_server=*/true,
                                                        nullptr, validation_context, hostname);

  if (validation_result.status !=
      Extensions::TransportSockets::Tls::ValidationResults::ValidationStatus::Successful) {
    ENVOY_LOG(warn, "Client certificate validation failed: {}",
              validation_result.error_details.value_or("unknown error"));
    *error_details = validation_result.error_details.value_or("Certificate validation failed");
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  }

  ENVOY_LOG(debug, "Client certificate validation succeeded for {} certificates", certs.size());
  *details = std::make_unique<CertVerifyResult>(true);
  return quic::QUIC_SUCCESS;
}

void EnvoyQuicServerProofVerifier::updateFilterChainManager(
    Network::FilterChainManager& filter_chain_manager) {
  filter_chain_manager_ = &filter_chain_manager;
}

} // namespace Quic
} // namespace Envoy
