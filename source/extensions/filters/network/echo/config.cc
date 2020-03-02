#include "envoy/extensions/filters/network/echo/v3/echo.pb.h"
#include "envoy/extensions/filters/network/echo/v3/echo.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/echo/echo.h"
#include "extensions/filters/network/well_known_names.h"

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
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::extensions::filters::network::echo::v3::Echo&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<EchoFilter>());
    };
  }

  bool isTerminalFilter() override { return true; }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
REGISTER_FACTORY(EchoConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.echo"};

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
