#include "contrib/golang/filters/network/source/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

/**
 * Static registration for the golang filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GolangConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.golang"};

Network::FilterFactoryCb GolangConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::golang::v3alpha::Config& proto_config,
    Server::Configuration::FactoryContext& context) {
  is_terminal_filter_ = proto_config.is_terminal_filter();

  UpstreamConn::initThreadLocalStorage(context, context.threadLocal());

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config);
  std::string configStr;
  auto res = config->pluginConfig().SerializeToString(&configStr);
  ASSERT(res, "SerializeToString should always successful");
  std::string libraryID = config->libraryID();

  Dso::DsoManager<Dso::NetworkFilterDsoImpl>::load(libraryID, config->libraryPath());

  uint64_t configID = 0;
  auto dlib = Dso::DsoManager<Dso::NetworkFilterDsoImpl>::getDsoByID(libraryID);
  if (dlib) {
    configID = dlib->envoyGoFilterOnNetworkFilterConfig(
        reinterpret_cast<unsigned long long>(libraryID.data()), libraryID.length(),
        reinterpret_cast<unsigned long long>(configStr.data()), configStr.length());
    ASSERT(configID, "config id should not be zero");
  }

  return [config, configID, &context, dlib](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(context, config, configID, dlib));
  };
}

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
