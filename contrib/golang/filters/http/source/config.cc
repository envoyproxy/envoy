#include "contrib/golang/filters/http/source/config.h"

#include "envoy/registry/registry.h"

#include "contrib/golang/filters/http/source/golang_filter.h"
#include "contrib/golang/filters/http/source/common/dso/dso.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

Http::FilterFactoryCb GolangFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config);

  handler_ = context.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::PostInit,
      [lib_id = config.library_id(), lib_path = config.library_path()] {
        Envoy::Dso::DsoInstanceManager::pub(lib_id, lib_path);
        ENVOY_LOG(info, "open golang library at postInit: {} {}", lib_id, lib_path);
      });

  return [&context, config](Http::FilterChainFactoryCallbacks& callbacks) {
    auto filter =
        std::make_shared<Filter>(context.grpcContext(), config, Filter::global_stream_id_++,
                                 Dso::DsoInstanceManager::getDsoInstanceByID(config->library_id()));
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
 * Static registration for the golang extensions filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GolangFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.golang"};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
