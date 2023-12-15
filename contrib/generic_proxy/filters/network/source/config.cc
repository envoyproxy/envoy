#include "contrib/generic_proxy/filters/network/source/config.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/tracing/tracer_manager_impl.h"

#include "access_log.h"
#include "contrib/generic_proxy/filters/network/source/rds.h"
#include "contrib/generic_proxy/filters/network/source/rds_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

SINGLETON_MANAGER_REGISTRATION(generic_route_config_provider_manager);

std::pair<CodecFactoryPtr, ProxyFactoryPtr>
Factory::factoriesFromProto(const envoy::config::core::v3::TypedExtensionConfig& codec_config,
                            Envoy::Server::Configuration::FactoryContext& context) {
  auto& factory = Config::Utility::getAndCheckFactory<CodecFactoryConfig>(codec_config);

  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(codec_config.typed_config(),
                                                context.messageValidationVisitor(), *message);
  return {factory.createCodecFactory(*message, context),
          factory.createProxyFactory(*message, context)};
}

Rds::RouteConfigProviderSharedPtr
Factory::routeConfigProviderFromProto(const ProxyConfig& config,
                                      Server::Configuration::FactoryContext& context,
                                      RouteConfigProviderManager& route_config_provider_manager) {
  if (config.has_generic_rds()) {
    if (config.generic_rds().config_source().config_source_specifier_case() ==
        envoy::config::core::v3::ConfigSource::kApiConfigSource) {
      const auto api_type = config.generic_rds().config_source().api_config_source().api_type();
      if (api_type != envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC &&
          api_type != envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC) {
        throw EnvoyException("genericrds supports only aggregated api_type in api_config_source");
      }
    }

    return route_config_provider_manager.createRdsRouteConfigProvider(
        config.generic_rds(), context.serverFactoryContext(), config.stat_prefix(),
        context.initManager());
  } else {
    return route_config_provider_manager.createStaticRouteConfigProvider(
        config.route_config(), context.serverFactoryContext());
  }
}

std::vector<NamedFilterFactoryCb> Factory::filtersFactoryFromProto(
    const ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& filters,
    const std::string stats_prefix, Envoy::Server::Configuration::FactoryContext& context) {
  std::vector<NamedFilterFactoryCb> factories;
  bool has_terminal_filter = false;
  std::string terminal_filter_name;
  for (const auto& filter : filters) {
    if (has_terminal_filter) {
      throw EnvoyException(fmt::format("Terminal filter: {} must be the last generic L7 filter",
                                       terminal_filter_name));
    }

    auto& factory = Config::Utility::getAndCheckFactory<NamedFilterConfigFactory>(filter);

    ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
    ASSERT(message != nullptr);
    Envoy::Config::Utility::translateOpaqueConfig(filter.typed_config(),
                                                  context.messageValidationVisitor(), *message);

    factories.push_back(
        {filter.name(), factory.createFilterFactoryFromProto(*message, stats_prefix, context)});

    if (factory.isTerminalFilter()) {
      terminal_filter_name = filter.name();
      has_terminal_filter = true;
    }
  }

  if (!has_terminal_filter) {
    throw EnvoyException("A terminal L7 filter is necessary for generic proxy");
  }
  return factories;
}

Envoy::Network::FilterFactoryCb
Factory::createFilterFactoryFromProtoTyped(const ProxyConfig& proto_config,
                                           Envoy::Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();

  std::shared_ptr<RouteConfigProviderManager> route_config_provider_manager =
      server_context.singletonManager().getTyped<RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(generic_route_config_provider_manager),
          [&server_context] {
            return std::make_shared<RouteConfigProviderManagerImpl>(server_context.admin());
          });

  auto tracer_manager = Tracing::TracerManagerImpl::singleton(context);

  auto factories = factoriesFromProto(proto_config.codec_config(), context);
  std::shared_ptr<ProxyFactory> custom_proxy_factory = std::move(factories.second);

  Tracing::TracerSharedPtr tracer;
  Tracing::ConnectionManagerTracingConfigPtr tracing_config;
  if (proto_config.has_tracing()) {
    if (proto_config.tracing().has_provider()) {
      tracer = tracer_manager->getOrCreateTracer(&proto_config.tracing().provider());
    }
    tracing_config = std::make_unique<Tracing::ConnectionManagerTracingConfigImpl>(
        context.listenerInfo().direction(), proto_config.tracing());
  }

  // Access log configuration.
  std::vector<AccessLogInstanceSharedPtr> access_logs;
  for (const auto& access_log : proto_config.access_log()) {
    AccessLogInstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::accessLoggerFromProto<FormatterContext>(access_log, context);
    access_logs.push_back(current_access_log);
  }

  const FilterConfigSharedPtr config = std::make_shared<FilterConfigImpl>(
      proto_config.stat_prefix(), std::move(factories.first),
      routeConfigProviderFromProto(proto_config, context, *route_config_provider_manager),
      filtersFactoryFromProto(proto_config.filters(), proto_config.stat_prefix(), context),
      std::move(tracer), std::move(tracing_config), std::move(access_logs), context);

  return [route_config_provider_manager, tracer_manager, config, &server_context,
          custom_proxy_factory](Envoy::Network::FilterManager& filter_manager) -> void {
    // Create filter by the custom filter factory if the custom filter factory is not null.
    if (custom_proxy_factory != nullptr) {
      custom_proxy_factory->createProxy(filter_manager, config);
      return;
    }

    filter_manager.addReadFilter(std::make_shared<Filter>(
        config, server_context.mainThreadDispatcher().timeSource(), server_context.runtime()));
  };
}

REGISTER_FACTORY(Factory, Envoy::Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
