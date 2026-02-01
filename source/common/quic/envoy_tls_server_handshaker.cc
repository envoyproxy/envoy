#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "source/common/common/assert.h"
#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace Envoy {
namespace Quic {

// SignatureCallback for async signature computation.
class EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::SignatureCallback
    : public quic::ProofSource::SignatureCallback {
public:
  explicit SignatureCallback(EnvoyProofSourceHandle* handle) : handle_(handle) {}

  void Run(bool ok, std::string signature,
           std::unique_ptr<quic::ProofSource::Details> details) override {
    if (handle_ == nullptr) {
      // Operation has been canceled, or Run has been called.
      return;
    }

    EnvoyProofSourceHandle* handle = handle_;
    handle_ = nullptr;

    handle->signature_callback_ = nullptr;
    if (handle->handshaker_ != nullptr) {
      handle->handshaker_->OnComputeSignatureDone(ok, is_sync_, std::move(signature),
                                                  std::move(details));
    }
  }

  void Cancel() { handle_ = nullptr; }
  void set_is_sync(bool is_sync) { is_sync_ = is_sync; }

private:
  EnvoyProofSourceHandle* handle_;
  bool is_sync_ = true;
};

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    EnvoyQuicProofSource* proof_source)
    : quic::TlsServerHandshaker(session, crypto_config), envoy_proof_source_(proof_source) {
  ASSERT(envoy_proof_source_ != nullptr);
}

std::unique_ptr<quic::ProofSourceHandle> EnvoyTlsServerHandshaker::MaybeCreateProofSourceHandle() {
  return std::make_unique<EnvoyProofSourceHandle>(this, envoy_proof_source_);
}

// EnvoyProofSourceHandle implementation

EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::EnvoyProofSourceHandle(
    EnvoyTlsServerHandshaker* handshaker, EnvoyQuicProofSource* proof_source)
    : handshaker_(handshaker), proof_source_(proof_source) {
  ASSERT(handshaker_ != nullptr);
  ASSERT(proof_source_ != nullptr);
}

EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::~EnvoyProofSourceHandle() { CloseHandle(); }

void EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::CloseHandle() {
  QUIC_DVLOG(1) << "EnvoyProofSourceHandle::CloseHandle, is_signature_pending="
                << (signature_callback_ != nullptr);
  if (signature_callback_) {
    signature_callback_->Cancel();
    signature_callback_ = nullptr;
  }
}

quic::QuicAsyncStatus EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::SelectCertificate(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const quic::QuicConnectionId& /*original_connection_id*/,
    absl::string_view /*ssl_capabilities*/, const std::string& hostname,
    const SSL_CLIENT_HELLO& client_hello, const std::string& /*alpn*/,
    absl::optional<std::string> /*alps*/, const std::vector<uint8_t>& /*quic_transport_params*/,
    const absl::optional<std::vector<uint8_t>>& /*early_data_context*/,
    const quic::QuicSSLConfig& /*ssl_config*/) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG(envoy_select_cert_detached) << "SelectCertificate called on a detached handle";
    return quic::QUIC_FAILURE;
  }

  auto transport_data =
      proof_source_->getTransportSocketAndFilterChain(server_address, client_address, hostname);

  if (transport_data.has_value()) {
    SSL_set_ex_data(client_hello.ssl, EnvoyQuicProofSource::filterChainExDataIndex(),
                    const_cast<Network::FilterChain*>(&transport_data->filter_chain_));

    auto& factory = dynamic_cast<const QuicServerTransportSocketFactory&>(
        transport_data->filter_chain_.transportSocketFactory());
    auto ticket_config = factory.getSessionTicketConfig();
    if (ticket_config.disable_stateless_resumption || !ticket_config.has_keys ||
        ticket_config.handles_session_resumption) {
      SSL_set_options(client_hello.ssl, SSL_OP_NO_TICKET);
    }
  }

  if (handshaker_->DoesOnSelectCertificateDoneExpectChains()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_use_proof_source_get_cert_chains, 1, 2);

    quic::ProofSource::CertChainsResult cert_chains_result =
        proof_source_->GetCertChains(server_address, client_address, hostname);

    handshaker_->OnSelectCertificateDone(
        /*ok=*/true, /*is_sync=*/true,
        quic::ProofSourceHandleCallback::LocalSSLConfig(cert_chains_result.chains,
                                                        quic::QuicDelayedSSLConfig()),
        /*ticket_encryption_key=*/absl::string_view(),
        /*cert_matched_sni=*/cert_chains_result.chains_match_sni);

    if (!handshaker_->select_cert_status().has_value()) {
      QUIC_BUG(envoy_select_cert_status_valueless_after_sync_select_cert);
      return quic::QUIC_SUCCESS;
    }
    return *handshaker_->select_cert_status();
  }

  bool cert_matched_sni = false;
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> chain =
      proof_source_->GetCertChain(server_address, client_address, hostname, &cert_matched_sni);

  handshaker_->OnSelectCertificateDone(
      /*ok=*/true, /*is_sync=*/true,
      quic::ProofSourceHandleCallback::LocalSSLConfig{chain.get(), quic::QuicDelayedSSLConfig()},
      /*ticket_encryption_key=*/absl::string_view(), cert_matched_sni);

  if (!handshaker_->select_cert_status().has_value()) {
    QUIC_BUG(envoy_select_cert_status_valueless)
        << "select_cert_status() has no value after a synchronous select cert";
    return quic::QUIC_SUCCESS;
  }
  return *handshaker_->select_cert_status();
}

quic::QuicAsyncStatus EnvoyTlsServerHandshaker::EnvoyProofSourceHandle::ComputeSignature(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, absl::string_view in,
    size_t max_signature_size) {
  if (!handshaker_ || !proof_source_) {
    QUIC_BUG(envoy_compute_sig_detached) << "ComputeSignature called on a detached handle";
    return quic::QUIC_FAILURE;
  }

  if (signature_callback_) {
    QUIC_BUG(envoy_compute_sig_pending) << "ComputeSignature called while pending";
    return quic::QUIC_FAILURE;
  }

  signature_callback_ = new SignatureCallback(this);
  proof_source_->ComputeTlsSignature(server_address, client_address, hostname, signature_algorithm,
                                     in, std::unique_ptr<SignatureCallback>(signature_callback_));

  if (signature_callback_) {
    QUIC_DVLOG(1) << "ComputeTlsSignature is pending";
    signature_callback_->set_is_sync(false);
    return quic::QUIC_PENDING;
  }

  bool success = handshaker_->HasValidSignature(max_signature_size);
  QUIC_DVLOG(1) << "ComputeTlsSignature completed synchronously. success:" << success;
  return success ? quic::QUIC_SUCCESS : quic::QUIC_FAILURE;
}

} // namespace Quic
} // namespace Envoy
