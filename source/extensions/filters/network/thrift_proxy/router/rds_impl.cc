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

std::string ConfigTraitsImpl::resourceType() const {
  return Envoy::Config::getResourceName<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>();
}

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createConfig() const {
  return std::make_shared<const NullConfigImpl>();
}

ProtobufTypes::MessagePtr ConfigTraitsImpl::createProto() const {
  return std::make_unique<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>();
}

const Protobuf::Message& ConfigTraitsImpl::validateResourceType(const Protobuf::Message& rc) const {
  return dynamic_cast<
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(rc);
}

const Protobuf::Message& ConfigTraitsImpl::validateConfig(const Protobuf::Message& rc) const {
  return rc;
}

const std::string& ConfigTraitsImpl::resourceName(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<
         const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration*>(&rc));
  return static_cast<
             const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(rc)
      .name();
}

Rds::ConfigConstSharedPtr ConfigTraitsImpl::createConfig(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<
         const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration*>(&rc));
  return std::make_shared<const ConfigImpl>(
      static_cast<const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(
          rc));
}

ProtobufTypes::MessagePtr ConfigTraitsImpl::cloneProto(const Protobuf::Message& rc) const {
  ASSERT(dynamic_cast<
         const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration*>(&rc));
  return std::make_unique<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(
      static_cast<const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&>(
          rc));
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin)
    : Rds::RouteConfigProviderManagerImpl(admin) {}

Rds::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::Trds& trds,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  const uint64_t manager_identifier = MessageUtil::hash(trds);

  auto existing_provider =
      reuseDynamicProvider(manager_identifier, init_manager, trds.route_config_name());

  if (existing_provider) {
    return existing_provider;
  }
  auto config_update =
      std::make_unique<Rds::RouteConfigUpdateReceiverImpl>(config_traits_, factory_context);
  auto resource_decoder = std::make_unique<Envoy::Config::OpaqueResourceDecoderImpl<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>>(
      factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
  auto subscription = std::make_shared<Rds::RdsRouteConfigSubscription>(
      std::move(config_update), std::move(resource_decoder), trds.config_source(),
      trds.route_config_name(), manager_identifier, factory_context, stat_prefix, *this);
  auto new_provider =
      std::make_shared<Rds::RdsRouteConfigProviderImpl>(std::move(subscription), factory_context);
  insertDynamicProvider(manager_identifier, new_provider,
                        &new_provider->subscription().initTarget(), init_manager);
  return new_provider;
}

Rds::RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
    Server::Configuration::ServerFactoryContext& factory_context) {
  auto provider = std::make_unique<Rds::StaticRouteConfigProviderImpl>(route_config, config_traits_,
                                                                       factory_context, *this);
  insertStaticProvider(provider.get());
  return provider;
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
