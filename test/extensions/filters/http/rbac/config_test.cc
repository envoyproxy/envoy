#include "extensions/filters/common/rbac/engine.h"
#include "extensions/filters/http/rbac/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {
namespace {

TEST(RoleBasedAccessControlFilterConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RoleBasedAccessControlFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::http::rbac::v2::RBAC(), "stats", context),
               ProtoValidationException);
}

TEST(RoleBasedAccessControlFilterConfigFactoryTest, ValidProto) {
  envoy::config::rbac::v2alpha::Policy policy;
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
  NiceMock<Server::Configuration::MockFactoryContext> context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  auto& cfg =
      dynamic_cast<envoy::config::filter::http::rbac::v2::RBACPerRoute&>(*proto_config.get());
  cfg.set_disabled(true);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, context);
  EXPECT_TRUE(route_config.get());

  const auto* inflated =
      dynamic_cast<const Filters::Common::RBAC::RoleBasedAccessControlEngine*>(route_config.get());
  EXPECT_NE(nullptr, inflated);
}

} // namespace
} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
