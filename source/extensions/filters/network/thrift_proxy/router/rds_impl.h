#pragma once

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/rds/config_traits.h"
#include "envoy/singleton/instance.h"

#include "source/common/rds/route_config_provider_manager.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class ConfigTraitsImpl : public Rds::ConfigTraits {
public:
  Rds::ConfigConstSharedPtr createNullConfig() const override;
  Rds::ConfigConstSharedPtr createConfig(const Protobuf::Message& rc) const override;
};

class ProtoTraitsImpl : public Rds::ProtoTraits {
public:
  ProtoTraitsImpl();
  const std::string& resourceType() const override { return resource_type_; };
  int resourceNameFieldNumber() const override { return 1; }
  ProtobufTypes::MessagePtr createEmptyProto() const override;

private:
  const std::string resource_type_;
};

class RouteConfigProviderManagerImpl : public RouteConfigProviderManager,
                                       public Singleton::Instance {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  // RouteConfigProviderManager
  Rds::RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::Trds& trds,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) override;

  Rds::RouteConfigProviderPtr createStaticRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

private:
  Rds::RouteConfigProviderManager manager_;
  ConfigTraitsImpl config_traits_;
  ProtoTraitsImpl proto_traits_;
};

using RouteConfigProviderManagerImplPtr = std::unique_ptr<RouteConfigProviderManagerImpl>;
using RouteConfigProviderManagerImplSharedPtr = std::shared_ptr<RouteConfigProviderManagerImpl>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
