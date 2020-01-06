#include "envoy/config/rbac/v3alpha/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3alpha/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3alpha/rbac.pb.validate.h"

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
  envoy::config::rbac::v3alpha::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  envoy::extensions::filters::http::rbac::v3alpha::RBAC config;
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
  auto* config = dynamic_cast<envoy::extensions::filters::http::rbac::v3alpha::RBAC*>(
      factory.createEmptyConfigProto().get());
  EXPECT_NE(nullptr, config);
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, EmptyRouteProto) {
  RoleBasedAccessControlFilterConfigFactory factory;
  auto* config = dynamic_cast<envoy::extensions::filters::http::rbac::v3alpha::RBACPerRoute*>(
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
