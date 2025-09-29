#pragma once

#include <string>

#include "envoy/server/filter_config.h"
#include "envoy/extensions/filters/network/sni_to_metadata/v3/sni_to_metadata.pb.h"
#include "envoy/extensions/filters/network/sni_to_metadata/v3/sni_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

/**
 * Config registration for the SNI to metadata filter. @see NamedNetworkFilterConfigFactory.
 */
class SniToMetadataFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter> {
public:
  SniToMetadataFilterFactory() : FactoryBase("envoy.filters.network.sni_to_metadata") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter& config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy