#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/quic/envoy_quic_proof_source.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    Envoy::Event::Dispatcher& dispatcher, const Network::DownstreamTransportSocketFactory& transport_socket_factory)
    : quic::TlsServerHandshaker(session, crypto_config), dispatcher_(dispatcher),
      proof_source_(crypto_config->proof_source()),
      transport_socket_factory_(dynamic_cast<const QuicServerTransportSocketFactory&>(transport_socket_factory)) {}

std::unique_ptr<quic::ProofSourceHandle> EnvoyTlsServerHandshaker::MaybeCreateProofSourceHandle() {
  return std::make_unique<EnvoyDefaultProofSourceHandle>(
    this, quic::TlsServerHandshaker::MaybeCreateProofSourceHandle(), transport_socket_factory_);
}

EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::EnvoyDefaultProofSourceHandle(
  EnvoyTlsServerHandshaker* tls_server_handshaker,
  std::unique_ptr<quic::ProofSourceHandle> default_proof_source_handle,
  const QuicServerTransportSocketFactory& transport_socket_factory) :
    handshaker_(tls_server_handshaker),
    default_proof_source_handle_(std::move(default_proof_source_handle)),
    transport_socket_factory_(transport_socket_factory) {
  ASSERT(handshaker_ != nullptr);
}

EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::~EnvoyDefaultProofSourceHandle() {
  CloseHandle();
}

// Close the handle. Cancel the pending signature operation, if any.
void EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::CloseHandle() {
  if (private_key_method_provider_ == nullptr) {
    default_proof_source_handle_->CloseHandle();
  }
}

// Delegates to proof_source_->GetCertChain.
// Returns QUIC_SUCCESS or QUIC_FAILURE. Never returns QUIC_PENDING.
quic::QuicAsyncStatus EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::SelectCertificate(
    const quic::QuicSocketAddress& server_address,
    const quic::QuicSocketAddress& client_address,
    const quic::QuicConnectionId& original_connection_id, absl::string_view ssl_capabilities,
    const std::string& hostname, absl::string_view client_hello, const std::string& alpn,
    std::optional<std::string> alps, const std::vector<uint8_t>& quic_transport_params,
    const std::optional<std::vector<uint8_t>>& early_data_context,
    const quic::QuicSSLConfig& ssl_config) {
  // TODO: Invoke transport socket factory to get the cert directly after the runtime flag
  // `envoy.restart_features.quic_handle_certs_with_shared_tls_code` removed.
  return default_proof_source_handle_->SelectCertificate(
    server_address, client_address, original_connection_id, ssl_capabilities,
    hostname, client_hello, alpn, alps, quic_transport_params, early_data_context, ssl_config);
}

quic::QuicAsyncStatus EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::ComputeSignature(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, absl::string_view in,
    size_t max_signature_size) {
  private_key_method_provider_ =
      transport_socket_factory_.getPrivateKeyMethodProvider(hostname);
    
  if (private_key_method_provider_ == nullptr) {
    return default_proof_source_handle_->ComputeSignature(server_address,
      client_address, hostname, signature_algorithm, in, max_signature_size);
  } else {
    auto* ssl = handshaker_->GetSsl();
    private_key_method_provider_->registerPrivateKeyMethod(ssl, *this, handshaker_->dispatcher());

    max_sig_size_ = max_signature_size;
    signature_.resize(max_sig_size_);
    size_t sig_size = 0;

    auto ret = private_key_method_provider_->getBoringSslPrivateKeyMethod()->sign(
        ssl, const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(signature_.data())), &sig_size,
        max_sig_size_, signature_algorithm,
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(in.data())), in.size());

    if (ret == ssl_private_key_failure) {
      return quic::QUIC_FAILURE;
    }

    if (ret == ssl_private_key_retry) {
      return quic::QUIC_PENDING;
    }

    return quic::QUIC_SUCCESS;
  }
}

void EnvoyTlsServerHandshaker::EnvoyDefaultProofSourceHandle::onPrivateKeyMethodComplete() {
  ASSERT(private_key_method_provider_ != nullptr);
  size_t sig_len = 0;
  auto ret = private_key_method_provider_->getBoringSslPrivateKeyMethod()->complete(
      handshaker_->GetSsl(), const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(signature_.data())), &sig_len,
      max_sig_size_);
  if (ret == ssl_private_key_failure) {
    handshaker_->OnComputeSignatureDone(
      false, false, "", nullptr);
  } else { 
    handshaker_->OnComputeSignatureDone(
                true, false, std::move(signature_), nullptr);
  }
  private_key_method_provider_->unregisterPrivateKeyMethod(handshaker_->GetSsl());
}

} // namespace Quic
} // namespace Envoy