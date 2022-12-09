#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/config.h"

#include <string>

#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;

ThriftProxy::ThriftFilters::FilterFactoryCb
PayloadToMetadataFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
        PayloadToMetadata& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ConfigSharedPtr filter_config(std::make_shared<Config>(proto_config));
  return
      [filter_config](ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addDecoderFilter(std::make_shared<PayloadToMetadataFilter>(filter_config));
      };
}

/**
 * Static registration for the header to metadata filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PayloadToMetadataFilterConfig,
                 ThriftProxy::ThriftFilters::NamedThriftFilterConfigFactory);

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
