#include "source/extensions/http/stateful_session/envelope/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {
namespace {

TEST(EnvelopeSessionStateFactoryConfigTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::SessionStateFactoryConfig>::getFactory(
      "envoy.http.stateful_session.envelope");
  ASSERT_NE(factory, nullptr);

  EnvelopeSessionStateProto proto_config;
  const std::string yaml = R"EOF(
    header:
      name: custom-header
    )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  EXPECT_NE(factory->createSessionStateFactory(proto_config, context), nullptr);
}

} // namespace
} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
