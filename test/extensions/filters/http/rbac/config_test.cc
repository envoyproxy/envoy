#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/http/rbac/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

TEST(RoleBasedAccessControlFilterConfigFactoryTest, ValidProto) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  envoy::config::filter::http::rbac::v2::RBAC config;
  (*config.mutable_rules()->mutable_policies())["foo"] = policy;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RoleBasedAccessControlFilterConfigFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, EmptyProto) {
  RoleBasedAccessControlFilterConfigFactory factory;
  auto* config = dynamic_cast<envoy::config::filter::http::rbac::v2::RBAC*>(
      factory.createEmptyConfigProto().get());
  EXPECT_NE(nullptr, config);
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, EmptyRouteProto) {
  RoleBasedAccessControlFilterConfigFactory factory;
  auto* config = dynamic_cast<envoy::config::filter::http::rbac::v2::RBACPerRoute*>(
      factory.createEmptyRouteConfigProto().get());
  EXPECT_NE(nullptr, config);
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, RouteSpecificConfig) {
  RoleBasedAccessControlFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(route_config.get());
}

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
