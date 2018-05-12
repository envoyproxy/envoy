#include "common/network/utility.h"

#include "extensions/filters/common/rbac/engine_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

void checkEngine(const RBAC::RBACEngineImpl& engine, bool expected_net, bool expected_http,
                 const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
                 const Envoy::Http::HeaderMap& headers = Envoy::Http::HeaderMapImpl()) {
  EXPECT_EQ(expected_net, engine.allowed(connection));
  EXPECT_EQ(expected_http, engine.allowed(connection, headers));
}

TEST(RBACEngineImpl, Disabled) {
  envoy::config::filter::http::rbac::v2::RBACPerRoute config;
  config.set_disabled(true);
  checkEngine(RBAC::RBACEngineImpl(config), true, true);
  checkEngine(RBAC::RBACEngineImpl(envoy::config::filter::http::rbac::v2::RBAC(), true), true,
              true);
}

TEST(RBACEngineImpl, AllowedWhitelist) {
  envoy::config::rbac::v2alpha::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::filter::http::rbac::v2::RBACPerRoute config;
  envoy::config::rbac::v2alpha::RBAC* rbac = config.mutable_rbac()->mutable_rules();
  rbac->set_action(envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac->mutable_policies())["foo"] = policy;

  RBAC::RBACEngineImpl engine(config);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));
  checkEngine(engine, true, true, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));
  checkEngine(engine, false, false, conn);
}

TEST(RBACEngineImpl, DeniedBlacklist) {
  envoy::config::rbac::v2alpha::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::filter::http::rbac::v2::RBACPerRoute config;
  envoy::config::rbac::v2alpha::RBAC* rbac = config.mutable_rbac()->mutable_rules();
  rbac->set_action(envoy::config::rbac::v2alpha::RBAC_Action::RBAC_Action_DENY);
  (*rbac->mutable_policies())["foo"] = policy;

  RBAC::RBACEngineImpl engine(config);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));
  checkEngine(engine, false, false, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));
  checkEngine(engine, true, true, conn);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
