#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

/**
 * Config registration for the header to metadata filter. @see NamedThriftFilterConfigFactory.
 */
class PayloadToMetadataFilterConfig : public ThriftProxy::ThriftFilters::FactoryBase<
                                          envoy::extensions::filters::network::thrift_proxy::
                                              filters::payload_to_metadata::v3::PayloadToMetadata> {
public:
  PayloadToMetadataFilterConfig() : FactoryBase("envoy.filters.thrift.payload_to_metadata") {}

private:
  ThriftProxy::ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
          PayloadToMetadata& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
