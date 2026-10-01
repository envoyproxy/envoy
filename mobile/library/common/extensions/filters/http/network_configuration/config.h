#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/extensions/filters/http/network_configuration/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

/**
 * Config registration for the network_configuration filter. @see NamedHttpFilterConfigFactory.
 */
class NetworkConfigurationFilterFactory
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration> {
public:
  NetworkConfigurationFilterFactory() : UnifiedFactoryBase("network_configuration") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration&
          config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(NetworkConfigurationFilterFactory);

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
