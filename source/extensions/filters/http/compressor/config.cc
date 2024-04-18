#include "source/extensions/filters/http/compressor/config.h"

#include "envoy/compression/compressor/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/compressor/compressor_filter.h"

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

Router::RouteSpecificFilterConfigConstSharedPtr
CompressorFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<CompressorPerRouteFilterConfig>(proto_config);
}

/**
 * Static registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(CompressorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
