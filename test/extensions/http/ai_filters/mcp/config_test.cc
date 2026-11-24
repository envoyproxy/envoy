#include <limits>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"
#include "source/extensions/http/ai_filters/mcp/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;
using McpProto = envoy::extensions::filters::http::mcp::v3::Mcp;
using McpOverrideProto = envoy::extensions::filters::http::mcp::v3::McpOverride;

class McpAiFilterConfigTest : public testing::Test {
public:
  McpFilterConfigFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

TEST_F(McpAiFilterConfigTest, IsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory("envoy.http.ai_filters.mcp");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory, testing::WhenDynamicCastTo<McpFilterConfigFactory*>(testing::NotNull()));
}

// The AI filter is configured with the HTTP filter's messages.
TEST_F(McpAiFilterConfigTest, TakesTheHttpFilterConfiguration) {
  EXPECT_THAT(factory_.configTypes(),
              testing::UnorderedElementsAre("envoy.extensions.filters.http.mcp.v3.Mcp",
                                            "envoy.extensions.filters.http.mcp.v3.McpOverride"));
}

TEST_F(McpAiFilterConfigTest, CreatesFilterFromEmptyConfig) {
  const auto factory_cb = factory_.createAiFilterFactory(*factory_.createEmptyConfigProto(),
                                                         context_, *stats_store_.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::Unspecified};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
  EXPECT_NE(TestUtility::findCounter(stats_store_, "ai_protocol_manager.mcp.requests_rejected"),
            nullptr);
}

TEST_F(McpAiFilterConfigTest, RejectsInvalidConfig) {
  McpProto proto;
  proto.mutable_parser_config()->add_methods();
  EXPECT_THROW(
      factory_.createAiFilterFactory(proto, context_, *stats_store_.rootScope()).IgnoreError(),
      ProtoValidationException);
}

TEST_F(McpAiFilterConfigTest, CreatesRouteConfig) {
  McpOverrideProto proto;
  proto.set_traffic_mode(McpProto::REJECT_NO_MCP);

  const auto route_config =
      factory_.createRouteConfig(proto, context_, context_.messageValidationVisitor());
  ASSERT_TRUE(route_config.ok()) << route_config.status();
  const auto* override_config =
      dynamic_cast<const HttpFilters::Mcp::McpOverrideConfig*>(route_config->get());
  ASSERT_NE(override_config, nullptr);
  EXPECT_EQ(override_config->trafficMode(), McpProto::REJECT_NO_MCP);
}

// Any string of a request within the body size limit is one the inspector may have to read.
TEST_F(McpAiFilterConfigTest, AsksForStringsUpToTheBodySizeLimitInline) {
  McpProto proto;
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), 8192);

  proto.mutable_max_request_body_size()->set_value(4096);
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), 4096);

  proto.mutable_max_request_body_size()->set_value(0);
  EXPECT_EQ(factory_.requestInlineStringBytes(proto), std::numeric_limits<uint32_t>::max());
}

} // namespace
} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
