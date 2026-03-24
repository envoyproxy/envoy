#include "source/extensions/filters/http/aws_eventstream_to_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/aws_eventstream_to_metadata/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamToMetadata {

absl::StatusOr<Http::FilterFactoryCb>
AwsEventstreamToMetadataConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::
        AwsEventstreamToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  // Create shared config (which instantiates the parser from TypedExtensionConfig)
  // Note: content_parser is validated as required by proto validation rules
  auto config = std::make_shared<FilterConfig>(proto_config, context.serverFactoryContext());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamEncoderFilter(std::make_shared<Filter>(config));
  };
}

/**
 * Static registration for the AWS EventStream to Metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsEventstreamToMetadataConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsEventstreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
