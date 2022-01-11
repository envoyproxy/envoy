#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3/header_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/header_to_metadata_filter.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

/**
 * Config registration for the header to metadata filter. @see NamedThriftFilterConfigFactory.
 */
class HeaderToMetadataFilterConfig : public ThriftProxy::ThriftFilters::FactoryBase<
                                         envoy::extensions::filters::network::thrift_proxy::
                                             filters::header_to_metadata::v3::HeaderToMetadata> {
public:
  HeaderToMetadataFilterConfig() : FactoryBase("envoy.filters.thrift.header_to_metadata") {}

private:
  ThriftProxy::ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
          HeaderToMetadata& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
