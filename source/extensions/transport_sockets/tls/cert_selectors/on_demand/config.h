#pragma once

#include "envoy/registry/registry.h"
#include "envoy/ssl/handshaker.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/secret_manager.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

class BaseAsyncSelector : protected Logger::Loggable<Logger::Id::connection> {
public:
  BaseAsyncSelector(std::shared_ptr<SecretManager>& secret_manager)
      : secret_manager_(secret_manager) {}

protected:
  Ssl::SelectionResult doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                          Ssl::CertificateSelectionCallbackPtr cb);
  std::shared_ptr<SecretManager> secret_manager_;
};

/**
 * An asynchronous certificate selector is created for each TLS socket on each worker.
 */
class AsyncSelector : public BaseAsyncSelector, public Ssl::TlsCertificateSelector {
public:
  AsyncSelector(Ssl::TlsCertificateMapperPtr&& mapper,
                std::shared_ptr<SecretManager>& secret_manager)
      : BaseAsyncSelector(secret_manager), mapper_(std::move(mapper)) {}

  // Ssl::TlsCertificateSelector
  bool providesCertificates() const override { return true; }
  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("Not supported with QUIC");
  };

private:
  Ssl::TlsCertificateMapperPtr mapper_;
};

class UpstreamAsyncSelector : public BaseAsyncSelector, public Ssl::UpstreamTlsCertificateSelector {
public:
  UpstreamAsyncSelector(Ssl::UpstreamTlsCertificateMapperPtr&& mapper,
                        std::shared_ptr<SecretManager>& secret_manager)
      : BaseAsyncSelector(secret_manager), mapper_(std::move(mapper)) {}
  // Ssl::UpstreamTlsCertificateSelector
  Ssl::SelectionResult
  selectTlsContext(const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
                   Ssl::CertificateSelectionCallbackPtr cb) override;

private:
  Ssl::UpstreamTlsCertificateMapperPtr mapper_;
};

class BaseCertificateSelectorFactory {
public:
  BaseCertificateSelectorFactory(std::shared_ptr<SecretManager>&& secret_manager)
      : secret_manager_(std::move(secret_manager)) {}
  absl::Status onConfigUpdate();

protected:
  std::shared_ptr<SecretManager> secret_manager_;
};

class OnDemandTlsCertificateSelectorFactory : public BaseCertificateSelectorFactory,
                                              public Ssl::TlsCertificateSelectorFactory {
public:
  OnDemandTlsCertificateSelectorFactory(Ssl::TlsCertificateMapperFactory&& mapper_factory,
                                        std::shared_ptr<SecretManager>&& secret_manager)
      : BaseCertificateSelectorFactory(std::move(secret_manager)),
        mapper_factory_(std::move(mapper_factory)) {}
  // Ssl::TlsCertificateSelectorFactory
  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override {
    return BaseCertificateSelectorFactory::onConfigUpdate();
  }

private:
  Ssl::TlsCertificateMapperFactory mapper_factory_;
};

class UpstreamOnDemandTlsCertificateSelectorFactory
    : public BaseCertificateSelectorFactory,
      public Ssl::UpstreamTlsCertificateSelectorFactory {
public:
  UpstreamOnDemandTlsCertificateSelectorFactory(
      Ssl::UpstreamTlsCertificateMapperFactory&& mapper_factory,
      std::shared_ptr<SecretManager>&& secret_manager)
      : BaseCertificateSelectorFactory(std::move(secret_manager)),
        mapper_factory_(std::move(mapper_factory)) {}
  // Ssl::UpstreamTlsCertificateSelectorFactory
  Ssl::UpstreamTlsCertificateSelectorPtr
  createUpstreamTlsCertificateSelector(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override {
    return BaseCertificateSelectorFactory::onConfigUpdate();
  }

private:
  Ssl::UpstreamTlsCertificateMapperFactory mapper_factory_;
};

class OnDemandTlsCertificateSelectorConfigFactory
    : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(OnDemandTlsCertificateSelectorConfigFactory);

class UpstreamOnDemandTlsCertificateSelectorConfigFactory
    : public Ssl::UpstreamTlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
  createUpstreamTlsCertificateSelectorFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context,
      const Ssl::ClientContextConfig& tls_config) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
