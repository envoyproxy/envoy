#include "envoy/config/filter/network/rbac/v2/rbac.pb.validate.h"

#include "extensions/filters/network/rbac/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

TEST(RoleBasedAccessControlNetworkFilterConfigFactoryTest, ValidProto) {
  envoy::config::rbac::v2alpha::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  envoy::config::filter::network::rbac::v2::RBAC config;
  config.set_stat_prefix("stats");
  (*config.mutable_rules()->mutable_policies())["foo"] = policy;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RoleBasedAccessControlNetworkFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RoleBasedAccessControlNetworkFilterConfigFactoryTest, EmptyProto) {
  RoleBasedAccessControlNetworkFilterConfigFactory factory;
  auto* config = dynamic_cast<envoy::config::filter::network::rbac::v2::RBAC*>(
      factory.createEmptyConfigProto().get());
  EXPECT_NE(nullptr, config);
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
