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
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::network::direct_response::v3::Config> {
public:
  DirectResponseConfigFactory()
      : ExceptionFreeFactoryBase(NetworkFilterNames::get().DirectResponse) {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::direct_response::v3::Config& config,
      Server::Configuration::FactoryContext& context) override {
    absl::StatusOr<std::string> content_or =
        Config::DataSource::read(config.response(), true, context.serverFactoryContext().api());
    RETURN_IF_NOT_OK_REF(content_or.status());
    return
        [content = std::move(content_or.value())](Network::FilterManager& filter_manager) -> void {
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
