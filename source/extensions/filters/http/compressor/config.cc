#include "source/extensions/filters/http/compressor/config.h"

#include "envoy/compression/compressor/config.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/network/address.h"

#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/compressor/compressor_filter.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

absl::StatusOr<Http::FilterFactoryCb> CompressorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(
      proto_config.compressor_library().typed_config().type_url())};
  Compression::Compressor::NamedCompressorLibraryConfigFactory* const config_factory =
      Registry::FactoryRegistry<
          Compression::Compressor::NamedCompressorLibraryConfigFactory>::getFactoryByType(type);
  if (config_factory == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Didn't find a registered implementation for type: '{}'", type));
  }
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.compressor_library().typed_config(), context.messageValidationVisitor(),
      *config_factory);
  Compression::Compressor::CompressorFactoryPtr compressor_factory =
      config_factory->createCompressorFactoryFromProto(*message, context);
  CompressorFilterConfigSharedPtr config = std::make_shared<CompressorFilterConfig>(
      proto_config, stats_prefix, context.scope(), context.serverFactoryContext().runtime(),
      std::move(compressor_factory));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CompressorFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
CompressorFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  // Validate per-route compressor library configuration before creating the config object.
  if (proto_config.has_overrides() && proto_config.overrides().has_compressor_library()) {
    const std::string type{TypeUtil::typeUrlToDescriptorFullName(
        proto_config.overrides().compressor_library().typed_config().type_url())};
    Compression::Compressor::NamedCompressorLibraryConfigFactory* const config_factory =
        Registry::FactoryRegistry<
            Compression::Compressor::NamedCompressorLibraryConfigFactory>::getFactoryByType(type);
    if (config_factory == nullptr) {
      return absl::InvalidArgumentError(fmt::format(
          "Didn't find a registered implementation for per-route compressor type: '{}'", type));
    }
  }

  Server::GenericFactoryContextImpl generic_context(context, validator);
  return std::make_shared<CompressorPerRouteFilterConfig>(proto_config, generic_context);
}

/**
 * Static registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(CompressorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
