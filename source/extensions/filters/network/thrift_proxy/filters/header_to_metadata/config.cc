#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/config.h"

#include <string>

#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/header_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;

ThriftProxy::ThriftFilters::FilterFactoryCb
HeaderToMetadataFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
        HeaderToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      std::make_shared<Config>(proto_config, context.serverFactoryContext().regexEngine()));
  return
      [filter_config](ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addDecoderFilter(std::make_shared<HeaderToMetadataFilter>(filter_config));
      };
}

/**
 * Static registration for the header to metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderToMetadataFilterConfig,
                 ThriftProxy::ThriftFilters::NamedThriftFilterConfigFactory);

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
