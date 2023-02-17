#include "contrib/envoy/extensions/filters/network/rust_echo/v3/echo.pb.h"
#include "contrib/envoy/extensions/filters/network/rust_echo/v3/echo.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "contrib/rust_echo/filters/network/source/echo.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::rust_echo::v3::RustEcho> {
public:
  EchoFactory() : FactoryBase("envoy.filters.network.rust_echo") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::rust_echo::v3::RustEcho&,
      Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<Filter>());
    };
  }

  bool
  isTerminalFilterByProtoTyped(const envoy::extensions::filters::network::rust_echo::v3::RustEcho&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
REGISTER_FACTORY(EchoFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
