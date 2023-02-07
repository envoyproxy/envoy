#include "envoy/extensions/filters/network/echo/v3/echo.pb.h"
#include "envoy/extensions/filters/network/echo/v3/echo.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/rust_executor/echo.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RustExecutor {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class RustExecutorFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::echo::v3::Echo> {
public:
  RustExecutorFactory() : FactoryBase(NetworkFilterNames::get().Echo) {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::extensions::filters::network::echo::v3::Echo&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<Filter>());
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
LEGACY_REGISTER_FACTORY(RustExecutorFactory, Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.echo");

} // namespace RustExecutor
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
