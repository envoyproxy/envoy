#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/assert.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/common/tls/server_ssl_socket.h"

namespace Envoy {
namespace Quic {

class QuicServerTransportSocketFactory: public Network::DownstreamTransportSocketFactory,
                                        public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Stats::Scope& store, const std::string& perspective) :
    QuicTransportSocketFactoryBase(store, perspective) {}
  ~QuicServerTransportSocketFactory() = default;

  virtual std::pair<quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>,
            std::shared_ptr<quic::CertificatePrivateKey>>
  getTlsCertificateAndKey(absl::string_view sni, bool* cert_matched_sni) const PURE;

  virtual Envoy::Ssl::PrivateKeyMethodProviderSharedPtr
  getPrivateKeyMethodProvider(absl::string_view sni) const PURE;

  virtual std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  legacyGetTlsCertificates() const PURE;

  virtual bool earlyDataEnabled() const PURE;

  virtual bool handleCertsWithSharedTlsCode() const PURE;
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactoryImpl: public QuicServerTransportSocketFactory {
public:
  ~QuicServerTransportSocketFactoryImpl() override;

  static absl::StatusOr<std::unique_ptr<QuicServerTransportSocketFactory>>
  create(bool enable_early_data, Stats::Scope& store, Ssl::ServerContextConfigPtr config,
         Envoy::Ssl::ContextManager& manager, const std::vector<std::string>& server_names);

  // Network::DownstreamTransportSocketFactory
  Network::TransportSocketPtr createDownstreamTransportSocket() const override {
    PANIC("not implemented");
  }
  bool implementsSecureTransport() const override { return true; }

  void initialize() override;

  std::pair<quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>,
            std::shared_ptr<quic::CertificatePrivateKey>>
  getTlsCertificateAndKey(absl::string_view sni, bool* cert_matched_sni) const override;
  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr
  getPrivateKeyMethodProvider(absl::string_view sni) const override;

  // Return TLS certificates if the context config is ready.
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  legacyGetTlsCertificates() const override {
    if (!config_->isReady()) {
      ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
      stats_.downstream_context_secrets_not_ready_.inc();
      return {};
    }
    return config_->tlsCertificates();
  }

  bool earlyDataEnabled() const override { return enable_early_data_; }

  bool handleCertsWithSharedTlsCode() const override { return handle_certs_with_shared_tls_code_; }

protected:
  QuicServerTransportSocketFactoryImpl(bool enable_early_data, Stats::Scope& store,
                                   Ssl::ServerContextConfigPtr config,
                                   Envoy::Ssl::ContextManager& manager,
                                   const std::vector<std::string>& server_names,
                                   absl::Status& creation_status);

  absl::Status onSecretUpdated() override;

private:
  absl::StatusOr<Envoy::Ssl::ServerContextSharedPtr> createSslServerContext() const;
  OptRef<const Ssl::TlsContext> getTlsContext(absl::string_view sni, bool* cert_matched_sni) const;

  const bool handle_certs_with_shared_tls_code_;
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
