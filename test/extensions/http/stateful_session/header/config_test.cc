#include "source/extensions/http/stateful_session/header/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {
namespace {

TEST(HeaderBasedSessionStateFactoryConfigTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::SessionStateFactoryConfig>::getFactory(
      "envoy.http.stateful_session.header");
  ASSERT_NE(factory, nullptr);

  HeaderBasedSessionStateProto proto_config;
  const std::string yaml = R"EOF(
        name: custom-header
    )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_NE(factory->createSessionStateFactory(proto_config, context), nullptr);
}

TEST(HeaderBasedSessionStateFactoryConfigTest, MissingHeaderName) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::SessionStateFactoryConfig>::getFactory(
      "envoy.http.stateful_session.header");
  ASSERT_NE(factory, nullptr);

  HeaderBasedSessionStateProto proto_config;
  const std::string yaml = R"EOF(
        name: ''
    )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_THROW(factory->createSessionStateFactory(proto_config, context), EnvoyException);
}

} // namespace
} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
