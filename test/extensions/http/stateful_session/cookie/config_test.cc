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

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.http.stateful_session.cookie
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState
      name: override_host
      path: /path
      ttl: 5s
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(factory->createSessionStateFactory(typed_config.typed_config(), context), nullptr);
}

} // namespace
} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
