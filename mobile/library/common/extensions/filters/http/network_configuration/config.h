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
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration> {
public:
  NetworkConfigurationFilterFactory() : FactoryBase("network_configuration") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration&
          config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(NetworkConfigurationFilterFactory);

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
