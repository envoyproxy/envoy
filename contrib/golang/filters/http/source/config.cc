#include "contrib/golang/filters/http/source/config.h"

#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"

#include "contrib/golang/filters/http/source/common/dso/dso.h"
#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

Http::FilterFactoryCb GolangFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config);

  ENVOY_LOG_MISC(info, "open golang library at parse config: {} {}", config->soId(),
                 config->soPath());

  auto res = Envoy::Dso::DsoInstanceManager::pub(config->soId(), config->soPath());
  if (!res) {
    throw EnvoyException(
        fmt::format("golang_filter: open library failed: {} {}", config->soId(), config->soPath()));
  }

  return [&context, config](Http::FilterChainFactoryCallbacks& callbacks) {
    auto filter =
        std::make_shared<Filter>(context.grpcContext(), config, Filter::global_stream_id_++,
                                 Dso::DsoInstanceManager::getDsoInstanceByID(config->soId()));
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
