#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/assert.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public Network::DownstreamTransportSocketFactory,
                                         public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(bool enable_early_data, Stats::Scope& store,
                                   Ssl::ServerContextConfigPtr config,
                                   Envoy::Ssl::ContextManager& manager,
                                   const std::vector<std::string>& server_names);
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

  // Return TLS certificates if the context config is ready.
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  legacyGetTlsCertificates() const {
    if (!config_->isReady()) {
      ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
      stats_.downstream_context_secrets_not_ready_.inc();
      return {};
    }
    return config_->tlsCertificates();
  }

  bool earlyDataEnabled() const { return enable_early_data_; }

protected:
  void onSecretUpdated() override;

private:
  Envoy::Ssl::ServerContextSharedPtr createSslServerContext() const;

  Envoy::Ssl::ContextManager& manager_;
  Stats::Scope& stats_scope_;
  Ssl::ServerContextConfigPtr config_;
  const std::vector<std::string> server_names_;
  mutable absl::Mutex ssl_ctx_mu_;
  Envoy::Ssl::ServerContextSharedPtr ssl_ctx_ ABSL_GUARDED_BY(ssl_ctx_mu_);
  bool enable_early_data_;
};

class QuicServerTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::DownstreamTransportSocketConfigFactory
  Network::DownstreamTransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;

  // Server::Configuration::TransportSocketConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(QuicServerTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
