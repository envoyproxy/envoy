#include "envoy/extensions/filters/network/echo/v3/echo.pb.h"
#include "envoy/extensions/filters/network/echo/v3/echo.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/echo/echo.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::echo::v3::Echo> {
public:
  EchoConfigFactory() : FactoryBase(NetworkFilterNames::get().Echo) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::echo::v3::Echo&,
      const Network::NetworkFilterMatcherSharedPtr& network_filter_matcher,
      Server::Configuration::FactoryContext&) override {
    return [network_filter_matcher](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(network_filter_matcher, std::make_shared<EchoFilter>());
    };
  }

  bool isTerminalFilterByProtoTyped(const envoy::extensions::filters::network::echo::v3::Echo&,
                                    Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(EchoConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.echo");

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
