#include "source/extensions/filters/network/dubbo_proxy/config.h"

#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/dubbo_proxy/router/rds.h"
#include "source/extensions/filters/network/dubbo_proxy/router/rds_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/stats.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

SINGLETON_MANAGER_REGISTRATION(dubbo_route_config_provider_manager);

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  auto& server_context = context.serverFactoryContext();

  std::shared_ptr<Router::RouteConfigProviderManager> route_config_provider_manager =
      server_context.singletonManager().getTyped<Router::RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(dubbo_route_config_provider_manager),
          [&server_context] {
            return std::make_shared<Router::RouteConfigProviderManagerImpl>(server_context.admin());
          });

  std::shared_ptr<Config> filter_config(
      std::make_shared<ConfigImpl>(proto_config, context, *route_config_provider_manager));

  return [route_config_provider_manager, filter_config,
          &server_context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ConnectionManager>(filter_config, server_context.api().randomGenerator(),
                                            server_context.mainThreadDispatcher().timeSource()));
  };
}

/**
 * Static registration for the dubbo filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DubboProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

class ProtocolTypeMapper {
public:
  using ConfigProtocolType = envoy::extensions::filters::network::dubbo_proxy::v3::ProtocolType;
  using ProtocolTypeMap = absl::flat_hash_map<ConfigProtocolType, ProtocolType>;

  static ProtocolType lookupProtocolType(ConfigProtocolType config_type) {
    const auto& iter = protocolTypeMap().find(config_type);
    ASSERT(iter != protocolTypeMap().end());
    return iter->second;
  }

private:
  static const ProtocolTypeMap& protocolTypeMap() {
    CONSTRUCT_ON_FIRST_USE(ProtocolTypeMap, {
                                                {ConfigProtocolType::Dubbo, ProtocolType::Dubbo},
                                            });
  }
};

class SerializationTypeMapper {
public:
  using ConfigSerializationType =
      envoy::extensions::filters::network::dubbo_proxy::v3::SerializationType;
  using SerializationTypeMap = absl::flat_hash_map<ConfigSerializationType, SerializationType>;

  static SerializationType lookupSerializationType(ConfigSerializationType type) {
    const auto& iter = serializationTypeMap().find(type);
    ASSERT(iter != serializationTypeMap().end());
    return iter->second;
  }

private:
  static const SerializationTypeMap& serializationTypeMap() {
    CONSTRUCT_ON_FIRST_USE(SerializationTypeMap,
                           {
                               {ConfigSerializationType::Hessian2, SerializationType::Hessian2},
                           });
  }
};

// class ConfigImpl.
ConfigImpl::ConfigImpl(const DubboProxyConfig& config,
                       Server::Configuration::FactoryContext& context,
                       Router::RouteConfigProviderManager& route_config_provider_manager)
    : context_(context), stats_prefix_(fmt::format("dubbo.{}.", config.stat_prefix())),
      stats_(DubboFilterStats::generateStats(stats_prefix_, context_.scope())),
      serialization_type_(
          SerializationTypeMapper::lookupSerializationType(config.serialization_type())),
      protocol_type_(ProtocolTypeMapper::lookupProtocolType(config.protocol_type())) {

  if (config.has_drds()) {
    if (config.route_config_size() > 0) {
      throw EnvoyException("both drds and route_config is present in DubboProxy");
    }
    if (config.drds().config_source().config_source_specifier_case() ==
        envoy::config::core::v3::ConfigSource::kApiConfigSource) {
      const auto api_type = config.drds().config_source().api_config_source().api_type();
      if (api_type != envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC &&
          api_type != envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC) {
        throw EnvoyException("drds supports only aggregated api_type in api_config_source");
      }
    }
    route_config_provider_ = route_config_provider_manager.createRdsRouteConfigProvider(
        config.drds(), context_.serverFactoryContext(), stats_prefix_, context_.initManager());
  } else if (config.has_multiple_route_config()) {
    if (config.route_config_size() > 0) {
      throw EnvoyException("both mutiple_route_config and route_config is present in DubboProxy");
    }
    route_config_provider_ = route_config_provider_manager.createStaticRouteConfigProvider(
        config.multiple_route_config(), context_.serverFactoryContext());
  } else {
    envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration
        multiple_route_config;

    *multiple_route_config.mutable_route_config() = config.route_config();
    route_config_provider_ = route_config_provider_manager.createStaticRouteConfigProvider(
        multiple_route_config, context_.serverFactoryContext());
  }

  if (config.dubbo_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::extensions::filters::network::dubbo_proxy::v3::DubboFilter router_config;
    router_config.set_name("envoy.filters.dubbo.router");
    registerFilter(router_config);
  } else {
    for (const auto& filter_config : config.dubbo_filters()) {
      registerFilter(filter_config);
    }
  }
}

void ConfigImpl::createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const DubboFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

Router::RouteConstSharedPtr ConfigImpl::route(const MessageMetadata& metadata,
                                              uint64_t random_value) const {
  auto config = std::static_pointer_cast<const Router::Config>(route_config_provider_->config());
  return config->route(metadata, random_value);
}

ProtocolPtr ConfigImpl::createProtocol() {
  return NamedProtocolConfigFactory::getFactory(protocol_type_).createProtocol(serialization_type_);
}

void ConfigImpl::registerFilter(const DubboFilterConfig& proto_config) {
  const auto& string_name = proto_config.name();
  ENVOY_LOG(debug, "    dubbo filter #{}", filter_factories_.size());
  ENVOY_LOG(debug, "      name: {}", string_name);
  ENVOY_LOG(debug, "    config: {}",
            MessageUtil::getJsonStringFromMessageOrError(proto_config.config()));

  auto& factory =
      Envoy::Config::Utility::getAndCheckFactoryByName<DubboFilters::NamedDubboFilterConfigFactory>(
          string_name);
  ProtobufTypes::MessagePtr message = factory.createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(proto_config.config(),
                                                context_.messageValidationVisitor(), *message);
  DubboFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
