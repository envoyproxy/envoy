#include "extensions/filters/network/original_src/original_src_config_factory.h"

#include "envoy/config/filter/network/original_src/v2alpha1/original_src.pb.h"
#include "envoy/config/filter/network/original_src/v2alpha1/original_src.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/original_src/config.h"
#include "extensions/filters/network/original_src/original_src.h"
#include "extensions/filters/network/well_known_names.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

OriginalSrcConfigFactory::OriginalSrcConfigFactory(): FactoryBase(NetworkFilterNames::get().OriginalSrc) {}

Network::FilterFactoryCb
OriginalSrcConfigFactory::createFilterFactoryFromProtoTyped(
  const envoy::config::filter::network::original_src::v2alpha1::OriginalSrc& proto_config,
                               Server::Configuration::FactoryContext&) {
  Config config(proto_config);
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<OriginalSrcFilter>(config));
  };
}

/**
 * Static registration for the original_src filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<OriginalSrcConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
