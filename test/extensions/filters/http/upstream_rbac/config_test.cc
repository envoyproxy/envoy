#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"

#include "source/extensions/filters/http/upstream_rbac/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {
namespace {

envoy::extensions::filters::http::rbac::v3::RBAC simpleConfig() {
  envoy::extensions::filters::http::rbac::v3::RBAC config;
  config.mutable_rules()->set_action(envoy::config::rbac::v3::RBAC::DENY);
  return config;
}

// The filter can be created as an upstream HTTP filter.
TEST(UpstreamRbacConfigTest, CreatesUpstreamFilter) {
  UpstreamRoleBasedAccessControlFilterConfigFactory factory;
  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> context;

  auto cb = factory.createFilterFactoryFromProto(simpleConfig(), "stats", context);
  EXPECT_TRUE(cb.ok());

  testing::NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;
  EXPECT_CALL(callbacks, addStreamDecoderFilter(testing::_));
  cb.value()(callbacks);
}

// The proto descriptor and a route-specific config can both be produced.
TEST(UpstreamRbacConfigTest, CreatesEmptyConfigProtoAndRouteConfig) {
  UpstreamRoleBasedAccessControlFilterConfigFactory factory;
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, factory.createEmptyRouteConfigProto());
}

} // namespace
} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
