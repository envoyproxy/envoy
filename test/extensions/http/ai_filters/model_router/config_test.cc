#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/type/ai/v3/llm_binding.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/model_router/config.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"

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
namespace ModelRouter {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;
using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

TEST(ModelRouterConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.model_router");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory,
              testing::WhenDynamicCastTo<ModelRouterFilterConfigFactory*>(testing::NotNull()));
}

TEST(ModelRouterConfigTest, CreatesFilterFromEmptyConfig) {
  ModelRouterFilterConfigFactory factory;
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

TEST(ModelRouterConfigTest, BuildsRoutesMapFromConfig) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  ModelRouterProto proto;

  auto* anthropic_prov = proto.add_providers();
  anthropic_prov->set_name("anthropic");
  anthropic_prov->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  anthropic_prov->set_host_and_port("api.anthropic.com");
  anthropic_prov->mutable_binding()->set_uri_pattern("/v1/messages");

  auto* openai_prov = proto.add_providers();
  openai_prov->set_name("openai");
  openai_prov->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  openai_prov->set_host_and_port("api.openai.com:443");

  auto* gemini_prov = proto.add_providers();
  gemini_prov->set_name("gemini");
  gemini_prov->set_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  gemini_prov->set_host_and_port("generativelanguage.googleapis.com");
  gemini_prov->mutable_binding()->set_uri_pattern(
      "/v1beta/models/{model}:{generateContent|streamGenerateContent}");

  auto* haiku_model = proto.add_models();
  haiku_model->set_name("haiku");
  haiku_model->set_provider("anthropic");
  haiku_model->set_model("claude-haiku-4-5");
  haiku_model->set_api_key("Bearer sk-ant");

  auto* gpt4o_mini_model = proto.add_models();
  gpt4o_mini_model->set_name("gpt4o_mini");
  gpt4o_mini_model->set_provider("openai");
  gpt4o_mini_model->set_model("gpt-4o-mini");
  gpt4o_mini_model->set_api_key("Bearer sk-oai");

  auto* gemini_pro_model = proto.add_models();
  gemini_pro_model->set_name("gemini_pro");
  gemini_pro_model->set_provider("gemini");
  gemini_pro_model->set_model("gemini-2.5-pro");
  gemini_pro_model->set_api_key("Bearer g-pro");

  auto* gemini_flash_model = proto.add_models();
  gemini_flash_model->set_name("gemini_flash");
  gemini_flash_model->set_provider("gemini");
  gemini_flash_model->set_model("gemini-2.5-flash");

  auto* entry1 = proto.add_routes();
  entry1->set_requested_model("fast-model");
  entry1->add_targets()->set_model("haiku");
  entry1->add_targets()->set_model("gpt4o_mini");

  auto* entry2 = proto.add_routes();
  entry2->set_requested_model("reasoning-model");
  entry2->add_targets()->set_model("gemini_pro");

  // A duplicate requested_model entry appends targets to the existing route list.
  auto* entry3 = proto.add_routes();
  entry3->set_requested_model("fast-model");
  entry3->add_targets()->set_model("gemini_flash");

  const auto config_or = ModelRouterFilterConfig::create(proto, *stats_store.rootScope());
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  const auto& config = *config_or.value();
  ASSERT_EQ(config.routes().size(), 2);

  const auto fast_it = config.routes().find("fast-model");
  ASSERT_NE(fast_it, config.routes().end());
  ASSERT_EQ(fast_it->second.routes_size(), 3);
  EXPECT_EQ(fast_it->second.routes(0).llm_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  EXPECT_EQ(fast_it->second.routes(0).model(), "claude-haiku-4-5");
  EXPECT_EQ(fast_it->second.routes(0).host_and_port(), "api.anthropic.com");
  EXPECT_EQ(fast_it->second.routes(0).api_key(), "Bearer sk-ant");
  ASSERT_TRUE(fast_it->second.routes(0).has_binding());
  EXPECT_EQ(fast_it->second.routes(0).binding().uri_pattern(), "/v1/messages");

  EXPECT_EQ(fast_it->second.routes(1).llm_protocol(), envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  EXPECT_EQ(fast_it->second.routes(1).model(), "gpt-4o-mini");
  EXPECT_EQ(fast_it->second.routes(1).host_and_port(), "api.openai.com:443");
  EXPECT_EQ(fast_it->second.routes(1).api_key(), "Bearer sk-oai");
  EXPECT_FALSE(fast_it->second.routes(1).has_binding());

  EXPECT_EQ(fast_it->second.routes(2).llm_protocol(), envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  EXPECT_EQ(fast_it->second.routes(2).model(), "gemini-2.5-flash");
  EXPECT_EQ(fast_it->second.routes(2).host_and_port(), "generativelanguage.googleapis.com");
  EXPECT_TRUE(fast_it->second.routes(2).api_key().empty());
  ASSERT_TRUE(fast_it->second.routes(2).has_binding());
  EXPECT_EQ(fast_it->second.routes(2).binding().uri_pattern(),
            "/v1beta/models/{model}:{generateContent|streamGenerateContent}");

  const auto reasoning_it = config.routes().find("reasoning-model");
  ASSERT_NE(reasoning_it, config.routes().end());
  ASSERT_EQ(reasoning_it->second.routes_size(), 1);
  EXPECT_EQ(reasoning_it->second.routes(0).llm_protocol(),
            envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  EXPECT_EQ(reasoning_it->second.routes(0).model(), "gemini-2.5-pro");
  EXPECT_EQ(reasoning_it->second.routes(0).host_and_port(), "generativelanguage.googleapis.com");
  EXPECT_EQ(reasoning_it->second.routes(0).api_key(), "Bearer g-pro");
  ASSERT_TRUE(reasoning_it->second.routes(0).has_binding());
  EXPECT_EQ(reasoning_it->second.routes(0).binding().uri_pattern(),
            "/v1beta/models/{model}:{generateContent|streamGenerateContent}");
}

TEST(ModelRouterConfigTest, RejectsUnknownModelReference) {
  ModelRouterFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  ModelRouterProto proto;

  auto* entry = proto.add_routes();
  entry->set_requested_model("fast-model");
  entry->add_targets()->set_model("nonexistent-model");

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_FALSE(factory_cb.ok());
  EXPECT_EQ(factory_cb.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(factory_cb.status().message()),
              testing::HasSubstr("Unknown model nonexistent-model"));
}

TEST(ModelRouterConfigTest, RejectsUnknownProviderReference) {
  ModelRouterFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  ModelRouterProto proto;

  auto* model = proto.add_models();
  model->set_name("haiku");
  model->set_provider("nonexistent-provider");
  model->set_model("claude-haiku-4-5");

  auto* entry = proto.add_routes();
  entry->set_requested_model("fast-model");
  entry->add_targets()->set_model("haiku");

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_FALSE(factory_cb.ok());
  EXPECT_EQ(factory_cb.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(factory_cb.status().message()),
              testing::HasSubstr("Unknown provider nonexistent-provider"));
}

TEST(ModelRouterConfigTest, RejectsInvalidLlmProtocolEnum) {
  ModelRouterFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  ModelRouterProto proto;

  auto* provider = proto.add_providers();
  provider->set_name("bad-provider");
  provider->set_llm_protocol(static_cast<envoy::type::ai::v3::LLMProtocol>(999));

  EXPECT_THROW_WITH_REGEX(
      factory.createAiFilterFactory(proto, context, *stats_store.rootScope()).IgnoreError(),
      EnvoyException, "value must be one of the defined enum values");
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
