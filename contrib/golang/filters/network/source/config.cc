#include "contrib/golang/filters/network/source/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

/**
 * Static registration for the golang filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GolangConfigFactory,
                 Envoy::Server::Configuration::NamedNetworkFilterConfigFactory);

Network::FilterFactoryCb GolangConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::golang::v3alpha::Config& proto_config,
    Server::Configuration::FactoryContext& context) {
  is_terminal_filter_ = proto_config.is_terminal_filter();

  UpstreamConn::initThreadLocalStorage(context, context.serverFactoryContext().threadLocal());

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config);
  std::string config_str;
  auto res = config->pluginConfig().SerializeToString(&config_str);
  ASSERT(res, "SerializeToString should always successful");
  std::string library_id = config->libraryID();

  // TODO(antJack): should check the return value and fast fail if we cannot load the dso.
  Dso::DsoManager<Dso::NetworkFilterDsoImpl>::load(library_id, config->libraryPath());

  uint64_t config_id = 0;
  auto dlib = Dso::DsoManager<Dso::NetworkFilterDsoImpl>::getDsoByID(library_id);
  if (dlib) {
    config_id = dlib->envoyGoFilterOnNetworkFilterConfig(
        reinterpret_cast<unsigned long long>(library_id.data()), library_id.length(),
        reinterpret_cast<unsigned long long>(config_str.data()), config_str.length());
    ASSERT(config_id, "config id should not be zero");
  }

  return [config, config_id, &context, dlib](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(context, config, config_id, dlib));
  };
}

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
