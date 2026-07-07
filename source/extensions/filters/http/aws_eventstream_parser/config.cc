#include "source/extensions/filters/http/aws_eventstream_parser/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/aws_eventstream_parser/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamParser {

absl::StatusOr<Http::FilterFactoryCb> AwsEventstreamParserConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  // Create shared config (which instantiates the parser from TypedExtensionConfig)
  // Note: content_parser is validated as required by proto validation rules
  auto config = std::make_shared<FilterConfig>(proto_config, context.serverFactoryContext());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamEncoderFilter(std::make_shared<Filter>(config));
  };
}

/**
 * Static registration for the AWS EventStream Parser filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsEventstreamParserConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
