#include "contrib/golang/filters/http/source/config.h"

#include <format>
#include <string>

#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/server/generic_factory_context.h"

#include "contrib/golang/common/dso/dso.h"
#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

absl::StatusOr<Http::FilterFactoryCb> GolangFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {

  ENVOY_LOG_MISC(debug, "load golang library at parse config: {} {}", proto_config.library_id(),
                 proto_config.library_path());

  // loads DSO store a static map and a open handles leak will occur when the filter gets loaded and
  // unloaded.
  // TODO: unload DSO when filter updated.
  auto dso_lib = Dso::DsoManager<Dso::HttpFilterDsoImpl>::load(
      proto_config.library_id(), proto_config.library_path(), proto_config.plugin_name());
  if (dso_lib == nullptr) {
    return absl::InvalidArgumentError(std::format("golang_filter: load library failed: {} {}",
                                                  proto_config.library_id(),
                                                  proto_config.library_path()));
  }

  Server::GenericFactoryContextImpl generic_context(
      context, extra_context.scope, extra_context.visitor, extra_context.init_manager);
  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      proto_config, dso_lib, std::format("{}golang.", extra_context.stats_prefix), generic_context);
  RETURN_IF_NOT_OK(config->newGoPluginConfig());
  return [config, dso_lib](Http::FilterChainFactoryCallbacks& callbacks) {
    const std::string& worker_name = callbacks.dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{id}");
    uint32_t worker_id;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_id)) {
      IS_ENVOY_BUG("failed to parse worker id from name");
    }
    auto filter = std::make_shared<Filter>(config, dso_lib, worker_id);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
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
