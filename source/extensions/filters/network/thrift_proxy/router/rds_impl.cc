#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/rds/route_config_update_receiver.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createNullConfig() const {
  return std::make_shared<const NullConfigImpl>();
}

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createConfig(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<
         const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration*>(&rc));
  return std::make_shared<const ConfigImpl>(
      static_cast<const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(
          rc));
}

ProtoTraitsImpl::ProtoTraitsImpl()
    : resource_type_(Envoy::Config::getResourceName<
                     envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>()) {
}

ProtobufTypes::MessagePtr ProtoTraitsImpl::createEmptyProto() const {
  return std::make_unique<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>();
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin)
    : manager_(admin, "thrift_routes", proto_traits_) {}

Rds::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::Trds& trds,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  return manager_.addDynamicProvider(
      trds, trds.route_config_name(), init_manager,
      [&factory_context, &trds, &stat_prefix, this](uint64_t manager_identifier) {
        auto config_update = std::make_unique<Rds::RouteConfigUpdateReceiverImpl>(
            config_traits_, proto_traits_, factory_context);
        auto resource_decoder = std::make_unique<Envoy::Config::OpaqueResourceDecoderImpl<
            envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
        auto subscription = std::make_shared<Rds::RdsRouteConfigSubscription>(
            std::move(config_update), std::move(resource_decoder), trds.config_source(),
            trds.route_config_name(), manager_identifier, factory_context, stat_prefix + "trds.",
            "TRDS", manager_);
        auto provider = std::make_shared<Rds::RdsRouteConfigProviderImpl>(std::move(subscription),
                                                                          factory_context);
        return std::make_pair(provider, &provider->subscription().initTarget());
      });
}

Rds::RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
    Server::Configuration::ServerFactoryContext& factory_context) {
  return manager_.addStaticProvider([&factory_context, &route_config, this]() {
    return std::make_unique<Rds::StaticRouteConfigProviderImpl>(route_config, config_traits_,
                                                                factory_context, manager_);
  });
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
