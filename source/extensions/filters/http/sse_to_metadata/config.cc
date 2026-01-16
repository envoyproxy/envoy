#include "source/extensions/filters/http/sse_to_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/sse_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {

absl::StatusOr<Http::FilterFactoryCb> SseToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  // Validate that content_parser is specified
  if (!proto_config.response_rules().has_content_parser()) {
    return absl::InvalidArgumentError("response_rules must have content_parser specified");
  }

  // Create shared config (which instantiates the parser from TypedExtensionConfig)
  auto config = std::make_shared<FilterConfig>(proto_config, context.serverFactoryContext());

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
