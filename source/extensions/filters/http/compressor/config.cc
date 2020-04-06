#include "extensions/filters/http/compressor/config.h"

#include "common/config/utility.h"

#include "extensions/filters/http/compressor/compressor_filter.h"
#include "extensions/filters/http/compressor/compressor_library_config.h"
#include "extensions/filters/http/compressor/gzip/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

Http::FilterFactoryCb CompressorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto& config_factory = Config::Utility::getAndCheckFactory<NamedCompressorLibraryConfigFactory>(
      proto_config.compressor_library());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.compressor_library().typed_config(), context.messageValidationVisitor(),
      config_factory);
  CompressorFactoryPtr compressor_factory =
      config_factory.createCompressorLibraryFromProto(*message, context);
  Common::Compressors::CompressorFilterConfigSharedPtr config =
      std::make_shared<CompressorFilterConfig>(proto_config, stats_prefix, context.scope(),
                                               context.runtime(), std::move(compressor_factory));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Common::Compressors::CompressorFilter>(config));
  };
}

/**
 * Static registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(CompressorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
