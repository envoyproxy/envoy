#include "source/extensions/filters/http/decompressor/config.h"

#include "envoy/compression/decompressor/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/decompressor/decompressor_filter.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

absl::StatusOr<Http::FilterFactoryCb> DecompressorFilterFactory::createFilterFactory(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::GenericFactoryContext& context) {
  const std::string decompressor_library_type{TypeUtil::typeUrlToDescriptorFullName(
      proto_config.decompressor_library().typed_config().type_url())};
  Compression::Decompressor::NamedDecompressorLibraryConfigFactory* const
      decompressor_library_factory = Registry::FactoryRegistry<
          Compression::Decompressor::NamedDecompressorLibraryConfigFactory>::
          getFactoryByType(decompressor_library_type);
  if (decompressor_library_factory == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Didn't find a registered implementation for type: '{}'", decompressor_library_type));
  }
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config.decompressor_library().typed_config(), context.messageValidationVisitor(),
      *decompressor_library_factory);
  Compression::Decompressor::DecompressorFactoryPtr decompressor_factory =
      decompressor_library_factory->createDecompressorFactoryFromProto(*message, context);
  DecompressorFilterConfigSharedPtr filter_config = std::make_shared<DecompressorFilterConfig>(
      proto_config, stats_prefix, context.scope(), context.serverFactoryContext().runtime(),
      std::move(decompressor_factory));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<DecompressorFilter>(filter_config));
  };
}

absl::StatusOr<Http::FilterFactoryCb> DecompressorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  return createFilterFactory(proto_config, stats_prefix, context);
}

absl::StatusOr<Http::FilterFactoryCb>
DecompressorFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context) {
  Server::GenericFactoryContextImpl generic_context(context, context.messageValidationVisitor());
  return createFilterFactory(proto_config, stats_prefix, generic_context);
}

/**
 * Static registration for the decompressor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(DecompressorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
