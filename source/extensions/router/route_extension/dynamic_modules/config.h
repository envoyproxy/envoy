#pragma once

#include "source/extensions/router/route_extension/dynamic_modules/route_extension.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

class DynamicModuleRouteExtensionFactory : public Envoy::Router::RouteExtensionFactory {
public:
  // Router::RouteExtensionFactory
  absl::StatusOr<Envoy::Router::RouteExtensionSharedPtr>
  createRouteExtension(const Protobuf::Message& config,
                       Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleRouteExtensionProto>();
  }

  std::string name() const override { return "envoy.router.route_extension.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
