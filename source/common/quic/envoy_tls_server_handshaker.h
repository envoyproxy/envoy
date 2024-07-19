#pragma once

#include "envoy/ssl/private_key/private_key_callbacks.h"

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           Envoy::Event::Dispatcher& dispatcher);

  Envoy::Event::Dispatcher& dispatcher() { return dispatcher_; }

protected:
  std::unique_ptr<quic::ProofSourceHandle> MaybeCreateProofSourceHandle() override;

private:
  class EnvoyDefaultProofSourceHandle : public quic::ProofSourceHandle,
                                        public Envoy::Ssl::PrivateKeyConnectionCallbacks,
                                        protected Logger::Loggable<Logger::Id::quic_stream> {
  public:
    EnvoyDefaultProofSourceHandle(EnvoyTlsServerHandshaker* handshaker,
                                  quic::ProofSource* proof_source,
                                  std::unique_ptr<quic::ProofSourceHandle> default_proof_source_handle);
    ~EnvoyDefaultProofSourceHandle();
    void CloseHandle() override;
    quic::QuicAsyncStatus SelectCertificate(
        const quic::QuicSocketAddress& server_address,
        const quic::QuicSocketAddress& client_address,
        const quic::QuicConnectionId& original_connection_id, absl::string_view ssl_capabilities,
        const std::string& hostname, absl::string_view client_hello, const std::string& alpn,
        std::optional<std::string> alps, const std::vector<uint8_t>& quic_transport_params,
        const std::optional<std::vector<uint8_t>>& early_data_context,
        const quic::QuicSSLConfig& ssl_config) override;

    quic::QuicAsyncStatus ComputeSignature(const quic::QuicSocketAddress& server_address,
                                           const quic::QuicSocketAddress& client_address,
                                           const std::string& hostname,
                                           uint16_t signature_algorithm, absl::string_view in,
                                           size_t max_signature_size) override;

  protected:
    ProofSourceHandleCallback* callback() override { return handshaker_; }

  private:
    void onPrivateKeyMethodComplete() override;

    EnvoyTlsServerHandshaker* handshaker_{nullptr};
    EnvoyQuicProofSource* proof_source_;
    std::unique_ptr<quic::ProofSourceHandle> default_proof_source_handle_{nullptr};
    size_t max_sig_size_{0};
    std::string signature_;
    Envoy::Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_provider_{nullptr};
    OptRef<const Network::FilterChain> filter_chain_;
  };

  Envoy::Event::Dispatcher& dispatcher_;
  quic::ProofSource* proof_source_;
};

} // namespace Quic
} // namespace Envoy