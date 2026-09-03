#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/router/route_producer.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/router/route_providers/dynamic_modules/route_provider.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {

class DynamicModuleRouteProviderFactory : public Envoy::Router::RouteProviderFactory {
public:
  absl::StatusOr<Envoy::Router::RouteProducerSharedPtr> createRouteProvider(
      const Protobuf::Message& config,
      const std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr>& route_templates,
      Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleRouteProviderProto>();
  }

  std::string name() const override { return "envoy.router.route_provider.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy
