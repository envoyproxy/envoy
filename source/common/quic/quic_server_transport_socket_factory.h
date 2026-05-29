#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/handshaker.h"

#include "source/common/common/assert.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/common/tls/server_ssl_socket.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public Network::DownstreamTransportSocketFactory,
                                         public QuicTransportSocketFactoryBase {
public:
  static absl::StatusOr<std::unique_ptr<QuicServerTransportSocketFactory>>
  create(bool enable_early_data, Stats::Scope& store, Ssl::ServerContextConfigPtr config,
         Envoy::Ssl::ContextManager& manager);
  ~QuicServerTransportSocketFactory() override;

  // Network::DownstreamTransportSocketFactory
  Network::TransportSocketPtr createDownstreamTransportSocket() const override {
    PANIC("not implemented");
  }
  bool implementsSecureTransport() const override { return true; }

  void initialize() override;

  std::pair<quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>,
            std::shared_ptr<quic::CertificatePrivateKey>>
  getTlsCertificateAndKey(absl::string_view sni, bool* cert_matched_sni) const;

  bool earlyDataEnabled() const { return enable_early_data_; }

  struct SessionTicketConfig {
    // True when session ticket encryption keys are explicitly configured via
    // session_ticket_keys or session_ticket_keys_sds_secret_config. Without
    // keys, the server cannot encrypt or decrypt session tickets.
    bool has_keys;
    // True when disable_stateless_session_resumption is set in
    // DownstreamTlsContext. When enabled, the server will not issue session
    // tickets and clients must perform full handshakes on every connection.
    bool disable_stateless_resumption;
    // True when an external mechanism (e.g., SDS provider) manages session
    // resumption including ticket encryption/decryption. When set, Envoy
    // should not install its own session ticket key processing callback.
    bool handles_session_resumption;
  };

  SessionTicketConfig getSessionTicketConfig() const {
    return {!config_->sessionTicketKeys().empty(), config_->disableStatelessSessionResumption(),
            config_->capabilities().handles_session_resumption};
  }

  // Returns the current ServerContextImpl, pinning a shared_ptr so it
  // remains valid for the caller's lifetime. May return null before
  // initialize() completes or if context creation failed.
  Ssl::ServerContextSharedPtr sslCtx() const {
    absl::ReaderMutexLock l(ssl_ctx_mu_);
    return ssl_ctx_;
  }

protected:
  QuicServerTransportSocketFactory(bool enable_early_data, Stats::Scope& store,
                                   Ssl::ServerContextConfigPtr config,
                                   Envoy::Ssl::ContextManager& manager,
                                   absl::Status& creation_status);

  absl::Status onSecretUpdated() override;

private:
  absl::StatusOr<Envoy::Ssl::ServerContextSharedPtr> createSslServerContext() const;

  Envoy::Ssl::ContextManager& manager_;
  Stats::Scope& stats_scope_;
  Ssl::ServerContextConfigPtr config_;
  mutable absl::Mutex ssl_ctx_mu_;
  Envoy::Ssl::ServerContextSharedPtr ssl_ctx_ ABSL_GUARDED_BY(ssl_ctx_mu_);
  bool enable_early_data_;
};

class QuicServerTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::DownstreamTransportSocketConfigFactory
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;

  // Server::Configuration::TransportSocketConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(QuicServerTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
