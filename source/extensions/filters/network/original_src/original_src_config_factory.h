#pragma once
#include "envoy/config/filter/network/original_src/v2alpha1/original_src.pb.h"

#include "extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {
/**
 * Config registration for the original_src filter.
 */
class OriginalSrcConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::original_src::v2alpha1::OriginalSrc> {
public:
  OriginalSrcConfigFactory();

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::original_src::v2alpha1::OriginalSrc& proto_config,
      Server::Configuration::FactoryContext&) override;
};

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
