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
    bool has_keys;
    bool disable_stateless_resumption;
    bool handles_session_resumption;
  };

  SessionTicketConfig getSessionTicketConfig() const {
    return {!config_->sessionTicketKeys().empty(), config_->disableStatelessSessionResumption(),
            config_->capabilities().handles_session_resumption};
  }

  int sessionTicketProcess(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                           HMAC_CTX* hmac_ctx, int encrypt) const;

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
