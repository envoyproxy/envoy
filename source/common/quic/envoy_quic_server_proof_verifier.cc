#include "source/common/quic/envoy_quic_server_proof_verifier.h"

#include "source/common/network/utility.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerProofVerifier::EnvoyQuicServerProofVerifier(
    Network::Socket& listen_socket, Network::FilterChainManager& filter_chain_manager,
    TimeSource& time_source)
    : listen_socket_(listen_socket), filter_chain_manager_(&filter_chain_manager),
      time_source_(time_source) {}

quic::QuicAsyncStatus EnvoyQuicServerProofVerifier::VerifyCertChain(
    const std::string& hostname, const uint16_t /*port*/,
    const std::vector<absl::string_view>& certs, const std::string& /*ocsp_response*/,
    const std::string& /*cert_sct*/, const quic::ProofVerifyContext* /*context*/,
    std::string* error_details, std::unique_ptr<quic::ProofVerifyDetails>* details,
    uint8_t* /*out_alert*/, std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) {
  ASSERT(error_details != nullptr);
  ASSERT(details != nullptr);

  auto fail = [&](absl::string_view msg) {
    ENVOY_LOG(warn, "QUIC client certificate validation rejected: {}", msg);
    *error_details = std::string(msg);
    *details = std::make_unique<CertVerifyResult>(false);
    return quic::QUIC_FAILURE;
  };

  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_mtls_server_enabled")) {
    return fail("QUIC mTLS server validation is disabled by runtime guard "
                "envoy.reloadable_features.quic_mtls_server_enabled");
  }

  auto local_addr = listen_socket_.connectionInfoProvider().localAddress();
  if (local_addr == nullptr) {
    return fail("listen socket has no local address");
  }

  // QUICHE invokes this verifier without the client's SNI or peer address, so a
  // connection socket is synthesized from the listener's local address and a
  // loopback peer for filter chain matching. Listeners that match filter chains
  // by SNI or peer address must keep a single trust anchor across all chains.
  // Otherwise the verifier resolves to the default chain's trust anchor.
  auto loopback_addr = local_addr->ip()->version() == Network::Address::IpVersion::v4
                           ? Network::Utility::getCanonicalIpv4LoopbackAddress()
                           : Network::Utility::getIpv6LoopbackAddress();
  auto server_quic_addr = envoyIpAddressToQuicSocketAddress(local_addr->ip());
  auto client_quic_addr = envoyIpAddressToQuicSocketAddress(loopback_addr->ip());

  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), server_quic_addr, client_quic_addr, hostname, "h3");
  if (connection_socket == nullptr) {
    return fail("failed to create connection socket for filter chain matching");
  }

  StreamInfo::StreamInfoImpl stream_info(time_source_,
                                         connection_socket->connectionInfoProviderSharedPtr(),
                                         StreamInfo::FilterState::LifeSpan::Connection);

  const Network::FilterChain* filter_chain =
      filter_chain_manager_->findFilterChain(*connection_socket, stream_info);
  if (filter_chain == nullptr) {
    return fail("no filter chain matched for QUIC client cert validation");
  }

  const auto* quic_factory = dynamic_cast<const QuicServerTransportSocketFactory*>(
      &filter_chain->transportSocketFactory());
  if (quic_factory == nullptr) {
    return fail("matched filter chain is not a QUIC transport socket factory");
  }

  if (!quic_factory->requiresClientCertificate()) {
    // Matched chain does not require a client certificate. Accept without validation.
    *details = std::make_unique<CertVerifyResult>(true);
    return quic::QUIC_SUCCESS;
  }

  if (certs.empty()) {
    return fail("client certificate required but not provided");
  }

  auto server_ssl_context = quic_factory->sslCtx();
  if (server_ssl_context == nullptr) {
    return fail("server SSL context not available, secrets are not loaded");
  }
  auto context_impl =
      std::dynamic_pointer_cast<Extensions::TransportSockets::Tls::ContextImpl>(server_ssl_context);
  ENVOY_BUG(context_impl != nullptr, "QUIC server SSL context is not a ContextImpl");
  if (context_impl == nullptr) {
    return fail("internal: server SSL context type mismatch");
  }

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (size_t i = 0; i < certs.size(); ++i) {
    std::string parse_error;
    bssl::UniquePtr<X509> cert = parseDERCertificate(certs[i], &parse_error);
    if (cert == nullptr) {
      return fail(absl::StrCat("failed to parse client certificate ", i, ": ", parse_error));
    }
    if (!bssl::PushToStack(cert_chain.get(), std::move(cert))) {
      return fail("failed to push client certificate onto stack");
    }
  }

  // Pass an empty `host_name`. The QUIC SNI is the server's identity and
  // forwarding it would cause `auto_sni_san_match` to compare it against the
  // client certificate SANs.
  Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext validation_ctx;
  validation_ctx.callbacks = nullptr;
  Extensions::TransportSockets::Tls::ValidationResults result =
      context_impl->customVerifyCertChainForQuic(*cert_chain, /*callback=*/nullptr,
                                                 /*is_server=*/true,
                                                 /*transport_socket_options=*/nullptr,
                                                 validation_ctx, /*host_name=*/std::string());

  if (result.status ==
      Extensions::TransportSockets::Tls::ValidationResults::ValidationStatus::Pending) {
    // Asynchronous validation is not wired through the QUIC verifier path. Reject
    // rather than silently accepting the certificate.
    return fail("asynchronous client certificate validation is not supported on QUIC");
  }
  if (result.status !=
      Extensions::TransportSockets::Tls::ValidationResults::ValidationStatus::Successful) {
    return fail(result.error_details.value_or("client certificate validation failed"));
  }

  ENVOY_LOG(debug, "QUIC client certificate validated, chain size {}.", certs.size());
  *details = std::make_unique<CertVerifyResult>(true);
  return quic::QUIC_SUCCESS;
}

void EnvoyQuicServerProofVerifier::updateFilterChainManager(
    Network::FilterChainManager& filter_chain_manager) {
  filter_chain_manager_ = &filter_chain_manager;
}

} // namespace Quic
} // namespace Envoy
