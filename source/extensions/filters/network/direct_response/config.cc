#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"
#include "envoy/extensions/filters/network/direct_response/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/direct_response/filter.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

/**
 * Config registration for the direct response filter. @see NamedNetworkFilterConfigFactory.
 */
class DirectResponseConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::direct_response::v3::Config> {
public:
  DirectResponseConfigFactory() : FactoryBase(NetworkFilterNames::get().DirectResponse) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::direct_response::v3::Config& config,
      Server::Configuration::FactoryContext& context) override {
    return [config, &context](Network::FilterManager& filter_manager) -> void {
      auto content = Config::DataSource::read(config.response(), true, context.api());
      filter_manager.addReadFilter(std::make_shared<DirectResponseFilter>(content));
    };
  }

  bool isTerminalFilterByProtoTyped(
      const envoy::extensions::filters::network::direct_response::v3::Config&,
      Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

/**
 * Static registration for the direct response filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DirectResponseConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
