#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

Ssl::SelectionResult
BaseAsyncSelector::doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                      Ssl::CertificateSelectionCallbackPtr cb) {
  auto current_context = secret_manager_->getContext(name);
  if (current_context) {
    ENVOY_LOG(trace, "Using an existing certificate '{}'", name);
    const Ssl::TlsContext* tls_context = &current_context.value()->tlsContext();
    const auto staple_action = ocspStapleAction(*tls_context, client_ocsp_capable,
                                                current_context.value()->ocspStaplePolicy());
    auto handle = std::make_shared<Handle>(*std::move(current_context));
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Success,
        .selected_ctx = tls_context,
        .staple = (staple_action == Ssl::OcspStapleAction::Staple),
        .handle = std::move(handle),
    };
  }
  ENVOY_LOG(trace, "Requesting a certificate '{}'", name);
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Pending,
      .handle = secret_manager_->fetchCertificate(name, std::move(cb), client_ocsp_capable),
  };
}

Ssl::SelectionResult AsyncSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                     Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromClientHello(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);
  return doSelectTlsContext(name, client_ocsp_capable, std::move(cb));
}

Ssl::SelectionResult UpstreamAsyncSelector::selectTlsContext(
    const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
    Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromServerHello(ssl, options);
  return doSelectTlsContext(name, false, std::move(cb));
}

Ssl::TlsCertificateSelectorPtr
OnDemandTlsCertificateSelectorFactory::create(Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<AsyncSelector>(mapper_factory_(), secret_manager_);
}

Ssl::UpstreamTlsCertificateSelectorPtr
UpstreamOnDemandTlsCertificateSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<UpstreamAsyncSelector>(mapper_factory_(), secret_manager_);
}

absl::Status BaseCertificateSelectorFactory::onConfigUpdate() {
  return secret_manager_->updateAll();
}

namespace {
template <typename MapperFactory, typename SelectorFactory>
absl::StatusOr<std::unique_ptr<SelectorFactory>>
createCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                 Server::Configuration::GenericFactoryContext& factory_context,
                                 AsyncContextFactory&& context_factory) {
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  MapperFactory& mapper_config =
      Config::Utility::getAndCheckFactory<MapperFactory>(config.certificate_mapper());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());
  // Doing this last since it can kick-start SDS fetches.
  // Envoy ensures that per-worker TLS sockets are destroyed before the filter
  // chain holding the TLS socket factory using a completion. This means the
  // TLS context config in the lambda will outlive each AsyncSelector, and it
  // is safe to refer to TLS context config by reference.
  auto secret_manager =
      std::make_shared<SecretManager>(config, factory_context, std::move(context_factory));
  return std::make_unique<SelectorFactory>(*std::move(mapper_factory), std::move(secret_manager));
}
} // namespace

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
OnDemandTlsCertificateSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError("Does not support QUIC listeners.");
  }
  // Session ID is currently generated from server names and the included TLS
  // certificates in the parent TLS context. It would not be safe to allow
  // resuming with this ID for on-demand TLS certificates which are not present
  // in the parent TLS context.
  if (!tls_config.disableStatelessSessionResumption() ||
      !tls_config.disableStatefulSessionResumption()) {
    return absl::InvalidArgumentError(
        "On demand certificates are not integrated with session resumption support.");
  }
  return createCertificateSelectorFactory<Ssl::TlsCertificateMapperConfigFactory,
                                          OnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ServerAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
UpstreamOnDemandTlsCertificateSelectorConfigFactory::createUpstreamTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ClientContextConfig& tls_config) {
  return createCertificateSelectorFactory<Ssl::UpstreamTlsCertificateMapperConfigFactory,
                                          UpstreamOnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ClientAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::TlsCertificateSelectorConfigFactory);

REGISTER_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::UpstreamTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
