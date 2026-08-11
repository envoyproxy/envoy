#include "source/extensions/filters/http/sse_to_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/sse_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {

absl::StatusOr<Http::FilterFactoryCb> SseToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  // This filter only uses the server factory context, so delegate to the server-context variant.
  return createHttpFilterFactoryFromProtoTyped(proto_config, stats_prefix,
                                               context.serverFactoryContext());
}

absl::StatusOr<Http::FilterFactoryCb> SseToMetadataConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& proto_config,
    const std::string&, Server::Configuration::ServerFactoryContext& context) {

  // Create shared config (which instantiates the parser from TypedExtensionConfig)
  // Note: content_parser is validated as required by proto validation rules
  auto config = std::make_shared<FilterConfig>(proto_config, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamEncoderFilter(std::make_shared<Filter>(config));
  };
}

/**
 * Static registration for the SSE to Metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SseToMetadataConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
