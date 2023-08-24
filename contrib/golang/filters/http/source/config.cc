#include "contrib/golang/filters/http/source/config.h"

#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"

#include "contrib/golang/common/dso/dso.h"
#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

Http::FilterFactoryCb GolangFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  ENVOY_LOG_MISC(debug, "load golang library at parse config: {} {}", proto_config.library_id(),
                 proto_config.library_path());

  // loads DSO store a static map and a open handles leak will occur when the filter gets loaded and
  // unloaded.
  // TODO: unload DSO when filter updated.
  auto dso_lib = Dso::DsoManager<Dso::HttpFilterDsoImpl>::load(
      proto_config.library_id(), proto_config.library_path(), proto_config.plugin_name());
  if (dso_lib == nullptr) {
    throw EnvoyException(fmt::format("golang_filter: load library failed: {} {}",
                                     proto_config.library_id(), proto_config.library_path()));
  }

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      proto_config, dso_lib, fmt::format("{}golang.", stats_prefix), context);
  config->newGoPluginConfig();
  return [config, dso_lib](Http::FilterChainFactoryCallbacks& callbacks) {
    auto filter = std::make_shared<Filter>(config, dso_lib);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
GolangFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config, context);
}

/**
 * Static registration for the Golang filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GolangFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
