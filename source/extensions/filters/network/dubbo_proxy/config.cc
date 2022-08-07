#include "source/extensions/filters/network/dubbo_proxy/config.h"

#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_manager_impl.h"
#include "source/common/tracing/tracer_config_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/dubbo_proxy/router/rds.h"
#include "source/extensions/filters/network/dubbo_proxy/router/rds_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/stats.h"

#include "absl/container/flat_hash_map.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

SINGLETON_MANAGER_REGISTRATION(dubbo_route_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(dubbo_tracer_config_manager);

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Router::RouteConfigProviderManager> route_config_provider_manager =
      context.singletonManager().getTyped<Router::RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(dubbo_route_config_provider_manager), [&context] {
            return std::make_shared<Router::RouteConfigProviderManagerImpl>(context.admin());
          });
  auto tracer_config_manager = context.singletonManager().getTyped<Tracing::HttpTracerManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(dubbo_tracer_config_manager), [&context] {
        return std::make_shared<Tracing::HttpTracerManagerImpl>(
            std::make_unique<Tracing::TracerFactoryContextImpl>(
                context.getServerFactoryContext(), context.messageValidationVisitor()));
      });

  std::shared_ptr<Config> filter_config(std::make_shared<ConfigImpl>(
      proto_config, context, *route_config_provider_manager, *tracer_config_manager));

  return [route_config_provider_manager, filter_config,
          &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ConnectionManager>(*filter_config, context.api().randomGenerator(),
                                            context.mainThreadDispatcher().timeSource()));
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
                       Router::RouteConfigProviderManager& route_config_provider_manager,
                       Tracing::HttpTracerManager& tracing_config_manager)
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
        config.drds(), context_.getServerFactoryContext(), stats_prefix_, context_.initManager());
  } else if (config.has_multiple_route_config()) {
    if (config.route_config_size() > 0) {
      throw EnvoyException("both mutiple_route_config and route_config is present in DubboProxy");
    }
    route_config_provider_ = route_config_provider_manager.createStaticRouteConfigProvider(
        config.multiple_route_config(), context_.getServerFactoryContext());
  } else {
    envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration
        multiple_route_config;

    *multiple_route_config.mutable_route_config() = config.route_config();
    route_config_provider_ = route_config_provider_manager.createStaticRouteConfigProvider(
        multiple_route_config, context_.getServerFactoryContext());
  }

  InitTracerCallback callback =
      [this, &context, &tracing_config_manager](const Protobuf::Message& message) -> void {
    this->initTracingConfig(message, context, tracing_config_manager);
  };

  if (config.dubbo_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::extensions::filters::network::dubbo_proxy::v3::DubboFilter router_config;
    router_config.set_name("envoy.filters.dubbo.router");
    registerFilter(router_config, callback);
  } else {
    for (const auto& filter_config : config.dubbo_filters()) {
      registerFilter(filter_config, callback);
    }
  }
}

std::unique_ptr<Http::TracingConnectionManagerConfig>
ConfigImpl::createTracingConfig(const envoy::extensions::filters::network::http_connection_manager::
                                    v3::HttpConnectionManager::Tracing& proto,
                                Server::Configuration::FactoryContext& context) {

  Tracing::OperationName tracing_operation_name;

  // Listener level traffic direction overrides the operation name
  switch (context.direction()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::core::v3::UNSPECIFIED: {
    // Continuing legacy behavior; if unspecified, we treat this as ingress.
    tracing_operation_name = Tracing::OperationName::Ingress;
    break;
  }
  case envoy::config::core::v3::INBOUND:
    tracing_operation_name = Tracing::OperationName::Ingress;
    break;
  case envoy::config::core::v3::OUTBOUND:
    tracing_operation_name = Tracing::OperationName::Egress;
    break;
  }

  Tracing::CustomTagMap custom_tags;
  for (const auto& tag : proto.custom_tags()) {
    custom_tags.emplace(tag.tag(), Tracing::CustomTagUtility::createCustomTag(tag));
  }

  envoy::type::v3::FractionalPercent client_sampling;
  client_sampling.set_numerator(proto.has_client_sampling() ? proto.client_sampling().value()
                                                            : 100);
  envoy::type::v3::FractionalPercent random_sampling;
  // TODO: Random sampling historically was an integer and default to out of 10,000. We should
  // deprecate that and move to a straight fractional percent config.
  uint64_t random_sampling_numerator{
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(proto, random_sampling, 10000, 10000)};
  random_sampling.set_numerator(random_sampling_numerator);
  random_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  envoy::type::v3::FractionalPercent overall_sampling;
  uint64_t overall_sampling_numerator{
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(proto, overall_sampling, 10000, 10000)};
  overall_sampling.set_numerator(overall_sampling_numerator);
  overall_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  const uint32_t max_path_tag_length =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto, max_path_tag_length, Tracing::DefaultMaxPathTagLength);

  return std::make_unique<Http::TracingConnectionManagerConfig>(
      Http::TracingConnectionManagerConfig{tracing_operation_name, custom_tags, client_sampling,
                                           random_sampling, overall_sampling, proto.verbose(),
                                           max_path_tag_length});
}

void ConfigImpl::initTracingConfig(const Protobuf::Message& message,
                                   Server::Configuration::FactoryContext& context,
                                   Tracing::HttpTracerManager& tracing_config_manager) {
  auto tracing_proto = MessageUtil::downcastAndValidate<const TracingProtoConfig&>(
      message, context.messageValidationVisitor());
  ASSERT(tracing_proto.has_provider());
  auto tracer = tracing_config_manager.getOrCreateHttpTracer(&tracing_proto.provider());
  auto tracing_config = createTracingConfig(tracing_proto, context);
  tracer_config_ = std::make_unique<Tracer::TracerConfigImpl>(std::move(tracing_config), tracer);
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

void ConfigImpl::registerFilter(const DubboFilterConfig& proto_config,
                                InitTracerCallback tracer_callback) {
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
  if (string_name == "envoy.filters.dubbo.tracer") {
    tracer_callback(*message);
  }
  DubboFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
