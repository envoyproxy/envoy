#include "source/extensions/filters/network/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Network::FilterFactoryCb>
DynamicModuleNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, FactoryContext& context) {
  // QUIC connections never flush the access logs of their network filters when they end.
  if (!proto_config.access_log().empty() && context.isQuic()) {
    return absl::InvalidArgumentError("access_log is not supported on QUIC listeners");
  }

  Server::Configuration::ServerFactoryContext& server_context = context.serverFactoryContext();
  const auto& module_config = proto_config.dynamic_module_config();
  // Network filters do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.filter_name(), server_context);
  RETURN_IF_NOT_OK_REF(load_result.status());
  auto dynamic_module = std::move(load_result->loaded);

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, proto_config.filter_name(),
          Extensions::DynamicModules::ConfigInitErrorStat);
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::NetworkFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::NetworkFilters::newDynamicModuleNetworkFilterConfig(
              proto_config.filter_name(), config, metrics_namespace, std::move(dynamic_module),
              server_context.clusterManager(), server_context.scope(),
              server_context.mainThreadDispatcher(),
              context.shouldBypassOverloadManager()
                  ? OptRef<Server::LoadShedPointProvider>()
                  : makeOptRef<Server::LoadShedPointProvider>(server_context.overloadManager()));

  if (!filter_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.filter_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create filter config: ", filter_config.status().message()));
  }

  for (const envoy::config::accesslog::v3::AccessLog& log_config : proto_config.access_log()) {
    filter_config.value()->access_logs_.push_back(
        AccessLog::AccessLogFactory::fromProto(log_config, context));
  }
  filter_config.value()->access_log_flush_interval_ =
      PROTOBUF_GET_OPTIONAL_MS(proto_config.access_log_options(), access_log_flush_interval);
  filter_config.value()->flush_access_log_on_start_ =
      proto_config.access_log_options().flush_access_log_on_start();
  filter_config.value()->random_generator_ = server_context.api().randomGenerator();

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    server_context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  return [config = filter_config.value()](Network::FilterManager& filter_manager) -> void {
    auto filter = std::make_shared<
        Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilter>(config);
    filter_manager.addFilter(filter);
    if (!config->access_logs_.empty()) {
      filter_manager.addAccessLogHandler(std::move(filter));
    }
  };
}

/**
 * Static registration for the dynamic modules network filter.
 */
REGISTER_FACTORY(DynamicModuleNetworkFilterConfigFactory, NamedNetworkFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
