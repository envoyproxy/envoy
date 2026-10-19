#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/config.h"

#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/tls_certificate_selector.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

namespace {

// Loads the dynamic module and creates the shared selector config from the typed config.
absl::StatusOr<DynamicModuleTlsCertificateSelectorConfigSharedPtr>
loadSelectorConfig(const DynamicModuleTlsCertificateSelectorProto& proto_config,
                   Server::Configuration::GenericFactoryContext& factory_context,
                   const Ssl::ContextConfig& tls_config, bool is_upstream,
                   AsyncContextFactory dynamic_context_factory) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  Server::Configuration::ServerFactoryContext& server_context =
      factory_context.serverFactoryContext();
  const std::string& selector_name = proto_config.selector_name();

  // No init manager is passed, so a remote module source is accepted only when it is already
  // cached on disk.
  auto load_result = Envoy::Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), selector_name, server_context);
  RETURN_IF_NOT_OK_REF(load_result.status());

  std::string selector_config;
  if (proto_config.has_selector_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.selector_config());
    if (!config_or_error.ok()) {
      Envoy::Extensions::DynamicModules::incrementLoadFailure(
          server_context, selector_name, Envoy::Extensions::DynamicModules::ConfigInitErrorStat);
      return config_or_error.status();
    }
    selector_config = std::move(config_or_error.value());
  }

  const std::string metrics_namespace =
      proto_config.dynamic_module_config().metrics_namespace().empty()
          ? std::string(DefaultMetricsNamespace)
          : proto_config.dynamic_module_config().metrics_namespace();
  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    server_context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  // The TLS context configuration creates its selector factory last, so the module can provide
  // dynamic certificates built from it during config_new.
  auto selector_config_or_error = newDynamicModuleTlsCertificateSelectorConfig(
      selector_name, selector_config, is_upstream, metrics_namespace,
      std::move(load_result->loaded), factory_context, tls_config,
      proto_config.allow_dynamic_certificates() ? std::move(dynamic_context_factory) : nullptr);
  if (!selector_config_or_error.ok()) {
    Envoy::Extensions::DynamicModules::incrementLoadFailure(
        server_context, selector_name, Envoy::Extensions::DynamicModules::ConfigInitErrorStat);
  }
  return selector_config_or_error;
}

} // namespace

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
DynamicModuleTlsCertificateSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError(
        "Dynamic module TLS certificate selector does not support QUIC listeners");
  }
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleTlsCertificateSelectorProto&>(
          config, factory_context.messageValidationVisitor());
  // The session resumption context of the TLS context does not cover the dynamic certificates.
  if (proto_config.allow_dynamic_certificates() &&
      (!tls_config.disableStatelessSessionResumption() ||
       !tls_config.disableStatefulSessionResumption())) {
    return absl::InvalidArgumentError(
        "Dynamic certificates require both stateless and stateful session resumption to be "
        "disabled");
  }
  auto selector_config = loadSelectorConfig(
      proto_config, factory_context, tls_config, false,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ServerAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
  RETURN_IF_NOT_OK_REF(selector_config.status());
  return std::make_unique<DynamicModuleTlsCertificateSelectorFactory>(
      std::move(selector_config.value()), tls_config);
}

absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
DynamicModuleUpstreamTlsCertificateSelectorConfigFactory::
    createUpstreamTlsCertificateSelectorFactory(
        const Protobuf::Message& config,
        Server::Configuration::GenericFactoryContext& factory_context,
        const Ssl::ClientContextConfig& tls_config) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const DynamicModuleTlsCertificateSelectorProto&>(
          config, factory_context.messageValidationVisitor());
  // The session cache of the TLS context is keyed by the server name, not by the certificate.
  if (proto_config.allow_dynamic_certificates() && tls_config.maxSessionKeys() != 0) {
    return absl::InvalidArgumentError("Dynamic certificates require max_session_keys to be 0");
  }
  auto selector_config = loadSelectorConfig(
      proto_config, factory_context, tls_config, true,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ClientAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
  RETURN_IF_NOT_OK_REF(selector_config.status());
  return std::make_unique<DynamicModuleUpstreamTlsCertificateSelectorFactory>(
      std::move(selector_config.value()));
}

REGISTER_FACTORY(DynamicModuleTlsCertificateSelectorConfigFactory,
                 Ssl::TlsCertificateSelectorConfigFactory);

REGISTER_FACTORY(DynamicModuleUpstreamTlsCertificateSelectorConfigFactory,
                 Ssl::UpstreamTlsCertificateSelectorConfigFactory);

} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
