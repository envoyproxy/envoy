#include "source/extensions/http/stateful_session/cookie/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {
namespace {

TEST(CookieBasedSessionStateFactoryConfigTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::SessionStateFactoryConfig>::getFactory(
      "envoy.http.stateful_session.cookie");
  ASSERT_NE(factory, nullptr);

  CookieBasedSessionStateProto proto_config;
  const std::string yaml = R"EOF(
    cookie:
      name: override_host
      path: /path
      ttl: 5s
  )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_NE(factory->createSessionStateFactory(proto_config, context), nullptr);
}

TEST(CookieBasedSessionStateFactoryConfigTest, NegativeTTL) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::SessionStateFactoryConfig>::getFactory(
      "envoy.http.stateful_session.cookie");
  ASSERT_NE(factory, nullptr);

  CookieBasedSessionStateProto proto_config;
  const std::string yaml = R"EOF(
    cookie:
      name: override_host
      path: /path
      ttl: -1s
  )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_THROW(factory->createSessionStateFactory(proto_config, context), EnvoyException);
}

} // namespace
} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
