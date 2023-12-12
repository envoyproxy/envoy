#pragma once

#include "source/common/quic/quic_transport_socket_factory.h"

namespace Envoy {

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public Network::DownstreamTransportSocketFactory,
                                         public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(bool enable_early_data, Stats::Scope& store,
                                   Ssl::ServerContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, "server"), config_(std::move(config)),
        enable_early_data_(enable_early_data) {}

  // Network::DownstreamTransportSocketFactory
  Network::TransportSocketPtr createDownstreamTransportSocket() const override {
    PANIC("not implemented");
  }
  bool implementsSecureTransport() const override { return true; }

  void initialize() override {
    config_->setSecretUpdateCallback([this]() {
      // The callback also updates config_ with the new secret.
      onSecretUpdated();
    });
  }

  // Return TLS certificates if the context config is ready.
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  getTlsCertificates() const {
    if (!config_->isReady()) {
      ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
      stats_.downstream_context_secrets_not_ready_.inc();
      return {};
    }
    return config_->tlsCertificates();
  }

  bool earlyDataEnabled() const { return enable_early_data_; }

protected:
  void onSecretUpdated() override { stats_.context_config_update_by_sds_.inc(); }

private:
  Ssl::ServerContextConfigPtr config_;
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

} // namespace Envoy
