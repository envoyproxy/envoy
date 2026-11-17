#pragma once

#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/router/route_specifier.h"

#include "source/extensions/router/route_specifiers/dynamic_modules/route_specifier.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {

/**
 * Config registration for the dynamic modules route specifier.
 */
class DynamicModuleRouteSpecifierFactory : public Envoy::Router::RouteSpecifierFactory {
public:
  // Router::RouteSpecifierFactory
  absl::StatusOr<Envoy::Router::RouteSpecifierSharedPtr>
  createRouteSpecifier(const Protobuf::Message& config,
                       Envoy::Router::RouteSpecifierFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<DynamicModuleRouteSpecifierProto>();
  }

  std::string name() const override { return "envoy.router.route_specifiers.dynamic_modules"; }
};

DECLARE_FACTORY(DynamicModuleRouteSpecifierFactory);

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
