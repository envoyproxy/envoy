#include "envoy/extensions/http/ai_filters/schema_validation/v3/schema_validation.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/schema_validation/config.h"
#include "source/extensions/http/ai_filters/schema_validation/filter.h"

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
namespace SchemaValidation {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;

TEST(SchemaValidationConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.schema_validation");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory,
              testing::WhenDynamicCastTo<SchemaValidationFilterConfigFactory*>(testing::NotNull()));
}

TEST(SchemaValidationConfigTest, CreatesFilterFromEmptyConfig) {
  SchemaValidationFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  const auto empty_proto = factory.createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);

  const auto factory_cb =
      factory.createAiFilterFactory(*empty_proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST(SchemaValidationConfigTest, DetectsAndFailsClosedByDefault) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::schema_validation::v3::SchemaValidation proto;
  const SchemaValidationFilterConfig defaults(proto, *stats_store.rootScope());
  EXPECT_EQ(defaults.defaultLlmProtocol(), LLMProtocol::Unspecified);
  EXPECT_FALSE(defaults.failOpen());

  proto.set_default_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  proto.set_fail_open(true);
  const SchemaValidationFilterConfig configured(proto, *stats_store.rootScope());
  EXPECT_EQ(configured.defaultLlmProtocol(), LLMProtocol::GeminiGenerateContent);
  EXPECT_TRUE(configured.failOpen());
}

} // namespace
} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
