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
CredentialInjectorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

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
          *message, stats_prefix + "credential_injector.", context);

  FilterConfigSharedPtr config =
      std::make_shared<FilterConfig>(std::move(credential_injector), proto_config.overwrite(),
                                     proto_config.allow_request_without_credential(),
                                     stats_prefix + "credential_injector.", context.scope());
  return [config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<CredentialInjectorFilter>(config));
  };
}

REGISTER_FACTORY(CredentialInjectorFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
