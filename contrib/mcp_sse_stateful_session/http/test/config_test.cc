#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/mcp_sse_stateful_session/http/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {
namespace Envelope {
namespace {

TEST(EnvelopeSessionStateFactoryConfigTest, BasicSse) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::McpSseSessionStateFactoryConfig>::getFactory(
          "envoy.http.mcp_sse_stateful_session.envelope");
  ASSERT_NE(factory, nullptr);

  EnvelopeSessionStateProto proto_config;
  const std::string yaml = R"EOF(
      param_name: custom-endpoint-param-name
      chunk_end_patterns: ["\r\n\r\n", "\n\n", "\r\r"]
    )EOF";
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(factory->createSessionStateFactory(proto_config, context), nullptr);
}

} // namespace
} // namespace Envelope
} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
