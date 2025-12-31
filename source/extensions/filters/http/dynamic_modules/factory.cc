#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, const std::string&, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {

  auto init_module_cb =
      [&](Extensions::DynamicModules::DynamicModulePtr& module_ptr) -> absl::Status {
    auto init_server_function =
        module_ptr->getFunctionPointer<decltype(&envoy_dynamic_module_on_server_init)>(
            "envoy_dynamic_module_on_server_init");

    // Make init_server_function optional - if the function is not found it should not block module loading.
    if (!init_server_function.ok()) {
      ENVOY_LOG_TO_LOGGER(
          Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
          "Failed to get function pointer for envoy_dynamic_module_on_server_init: {}",
          init_server_function.status().message());
      return absl::OkStatus();
    }

    auto success = (*init_server_function.value())(&context);
    if (!success) {
      return absl::InvalidArgumentError(
          "Dynamic module envoy_dynamic_module_on_server_init failed");
    }
    return absl::OkStatus();
  };
  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally(),
      init_module_cb);
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }
  absl::StatusOr<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
              proto_config.filter_name(), config, proto_config.terminal_filter(),
              std::move(dynamic_module.value()), dual_info.scope, context);

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::DynamicModules::HttpFilters::CustomStatNamespace);

  return [config = filter_config.value()](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    const std::string& worker_name = callbacks.dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{id}");
    uint32_t worker_index;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
      IS_ENVOY_BUG("failed to parse worker index from name");
    }
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            config, config->stats_scope_->symbolTable(), worker_index);
    filter->initializeInModuleFilter();
    callbacks.addStreamFilter(filter);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
DynamicModuleConfigFactory::createRouteSpecificFilterConfigTyped(
    const RouteConfigProto& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {

  auto init_module_cb =
      [&](Extensions::DynamicModules::DynamicModulePtr& module_ptr) -> absl::Status {
    auto init_server_function =
        module_ptr->getFunctionPointer<decltype(&envoy_dynamic_module_on_server_init)>(
            "envoy_dynamic_module_on_server_init");
    RETURN_IF_NOT_OK_REF(init_server_function.status());

    auto success = (*init_server_function.value())(&context);
    if (!success) {
      return absl::InvalidArgumentError(
          "Dynamic module envoy_dynamic_module_on_server_init failed");
    }
    return absl::OkStatus();
  };

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally(),
      init_module_cb);
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<Envoy::Extensions::DynamicModules::HttpFilters::
                     DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
              proto_config.per_route_config_name(), config, std::move(dynamic_module.value()));

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create pre-route filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return filter_config.value();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
