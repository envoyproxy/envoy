#include "source/extensions/filters/http/credential_injector/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/http/injected_credentials/common/factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

using Envoy::Extensions::Http::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory;

absl::StatusOr<Envoy::Http::FilterFactoryCb>
CredentialInjectorFilterFactory::createFilterFactoryFromProtoHelper(
    const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope, Init::Manager& init_manager) const {

  // Find the credential injector factory.
  auto* config_factory = Envoy::Config::Utility::getFactory<NamedCredentialInjectorConfigFactory>(
      proto_config.credential());
  if (config_factory == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Didn't find a registered implementation for '{}' with type URL: '{}'",
        proto_config.credential().name(),
        Envoy::Config::Utility::getFactoryType(proto_config.credential().typed_config())));
  }

  // create the credential injector
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.credential().typed_config(), context.messageValidationVisitor(),
      *config_factory);
  CredentialInjectorSharedPtr credential_injector =
      config_factory->createCredentialInjectorFromProto(
          *message, stats_prefix + "credential_injector.", context, init_manager);

  FilterConfigSharedPtr config =
      std::make_shared<FilterConfig>(std::move(credential_injector), proto_config.overwrite(),
                                     proto_config.allow_request_without_credential(),
                                     stats_prefix + "credential_injector.", scope);
  return [config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CredentialInjectorFilter>(config));
  };
}

absl::StatusOr<Envoy::Http::FilterFactoryCb>
CredentialInjectorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector&
        proto_config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {
  return createFilterFactoryFromProtoHelper(proto_config, stats_prefix, context, dual_info.scope,
                                            dual_info.init_manager);
}

Envoy::Http::FilterFactoryCb
CredentialInjectorFilterFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context) {
  auto result = createFilterFactoryFromProtoHelper(proto_config, stats_prefix, context,
                                                   context.scope(), context.initManager());
  if (!result.ok()) {
    ExceptionUtil::throwEnvoyException(std::string(result.status().message()));
  }
  return std::move(result.value());
}

REGISTER_FACTORY(CredentialInjectorFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamCredentialInjectorFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
