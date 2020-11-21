#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.validate.h"

#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/http/rbac/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

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
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  envoy::extensions::filters::http::rbac::v3::RBAC config;
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
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::filters::http::rbac::v3::RBAC*>(
                         factory.createEmptyConfigProto().get()));
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, EmptyRouteProto) {
  RoleBasedAccessControlFilterConfigFactory factory;
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::filters::http::rbac::v3::RBACPerRoute*>(
                         factory.createEmptyRouteConfigProto().get()));
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
