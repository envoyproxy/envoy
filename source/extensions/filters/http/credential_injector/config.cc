#include "source/extensions/filters/http/credential_injector/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/injected_credentials/common/factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

using Envoy::Extensions::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory;

Http::FilterFactoryCb CredentialInjectorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  // Find the credential injector factory.
  const std::string type{
      TypeUtil::typeUrlToDescriptorFullName(proto_config.credential().typed_config().type_url())};
  NamedCredentialInjectorConfigFactory* const config_factory =
      Registry::FactoryRegistry<NamedCredentialInjectorConfigFactory>::getFactoryByType(type);
  if (config_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }

  // create the credential injector
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.credential().typed_config(), context.messageValidationVisitor(),
      *config_factory);
  CredentialInjectorSharedPtr credential_injector =
      config_factory->createCredentialInjectorFromProto(*message, context);

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      std::move(credential_injector), proto_config.overwrite(),
      proto_config.allow_request_without_credential(), stats_prefix, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CredentialInjectorFilter>(config));
  };
}

REGISTER_FACTORY(CredentialInjectorFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
