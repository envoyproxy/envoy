#include "source/extensions/filters/http/stream_to_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/stream_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {

absl::StatusOr<Http::FilterFactoryCb> StreamToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  auto config = std::make_shared<FilterConfig>(proto_config, context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamEncoderFilter(std::make_shared<Filter>(config));
  };
}

/**
 * Static registration for the Stream to Metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(StreamToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
