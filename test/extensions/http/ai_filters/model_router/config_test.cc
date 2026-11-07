#include <string>
#include <vector>

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
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterFactoryCb;
using HttpFilters::AiProtocolManager::LLMProtocol;
using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

class ModelRouterConfigTest : public testing::Test {
public:
  ModelRouterProto parseYaml(const std::string& yaml) {
    ModelRouterProto proto;
    TestUtility::loadFromYaml(yaml, proto);
    return proto;
  }

  absl::StatusOr<std::shared_ptr<ModelRouterFilterConfig>> createConfig(const std::string& yaml) {
    return ModelRouterFilterConfig::create(parseYaml(yaml), *api_, *stats_store_.rootScope());
  }

  absl::StatusOr<AiFilterFactoryCb> createFactory(const ModelRouterProto& proto) {
    return factory_.createAiFilterFactory(proto, context_, *stats_store_.rootScope());
  }

  absl::StatusOr<AiFilterFactoryCb> createFactoryFromYaml(const std::string& yaml) {
    return createFactory(parseYaml(yaml));
  }

  Api::ApiPtr api_{Api::createApiForTest()};
  ModelRouterFilterConfigFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

TEST_F(ModelRouterConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.model_router");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory,
              testing::WhenDynamicCastTo<ModelRouterFilterConfigFactory*>(testing::NotNull()));
}

TEST_F(ModelRouterConfigTest, CreatesFilterFromEmptyConfig) {
  const auto empty_proto = factory_.createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);

  const auto factory_cb =
      factory_.createAiFilterFactory(*empty_proto, context_, *stats_store_.rootScope());
  ASSERT_TRUE(factory_cb.ok());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST_F(ModelRouterConfigTest, BuildsRoutesMapFromConfig) {
  const auto config_or = createConfig(R"EOF(
providers:
  - name: anthropic
    llm_protocol: ANTHROPIC_MESSAGES
    host_and_port: api.anthropic.com
    binding:
      uri_pattern: /v1/messages
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com:443
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    api_key:
      inline_string: "Bearer sk-ant"
  - name: opus
    provider: anthropic
    model: claude-opus-4-5
  - name: gpt4o_mini
    provider: openai
    model: gpt-4o-mini
    api_key:
      inline_string: "Bearer sk-oai"
routes:
  - requested_model: fast-model
    targets:
      - model: haiku
  - requested_model: openai-model
    targets:
      - model: gpt4o_mini
  # A duplicate requested_model entry appends targets to the existing route list.
  - requested_model: fast-model
    targets:
      - model: opus
)EOF");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  const auto& routes = config_or.value()->routes();
  ASSERT_EQ(routes.size(), 2);

  const auto fast_it = routes.find("fast-model");
  ASSERT_NE(fast_it, routes.end());
  ASSERT_NE(fast_it->second, nullptr);
  ASSERT_EQ(fast_it->second->routes().size(), 2);

  const auto& r0 = fast_it->second->routes()[0];
  EXPECT_EQ(r0.llm_protocol, LLMProtocol::AnthropicMessages);
  EXPECT_EQ(r0.model, "claude-haiku-4-5");
  EXPECT_EQ(r0.provider, "anthropic");
  EXPECT_EQ(r0.host_and_port, "api.anthropic.com");
  EXPECT_EQ(r0.api_key, "Bearer sk-ant");
  ASSERT_NE(r0.binding, nullptr);
  EXPECT_EQ(r0.binding->pattern()->source(), "/v1/messages");

  const auto& r1 = fast_it->second->routes()[1];
  EXPECT_EQ(r1.llm_protocol, LLMProtocol::AnthropicMessages);
  EXPECT_EQ(r1.model, "claude-opus-4-5");
  EXPECT_EQ(r1.provider, "anthropic");
  EXPECT_EQ(r1.host_and_port, "api.anthropic.com");
  EXPECT_TRUE(r1.api_key.empty());
  ASSERT_NE(r1.binding, nullptr);
  EXPECT_EQ(r1.binding->pattern()->source(), "/v1/messages");

  const auto openai_it = routes.find("openai-model");
  ASSERT_NE(openai_it, routes.end());
  ASSERT_NE(openai_it->second, nullptr);
  ASSERT_EQ(openai_it->second->routes().size(), 1);
  EXPECT_EQ(openai_it->second->routes()[0].llm_protocol, LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(openai_it->second->routes()[0].model, "gpt-4o-mini");
  EXPECT_EQ(openai_it->second->routes()[0].provider, "openai");
  EXPECT_EQ(openai_it->second->routes()[0].host_and_port, "api.openai.com:443");
  EXPECT_EQ(openai_it->second->routes()[0].api_key, "Bearer sk-oai");
  EXPECT_EQ(openai_it->second->routes()[0].binding, nullptr);
}

TEST_F(ModelRouterConfigTest, ReadsAndTrimsApiKeyFromDataSource) {
  const std::string key_file = TestEnvironment::writeStringToFileForTest(
      "model_router_api_key.txt", " \tBearer file-sk-123 \r\n");

  const auto config_or = createConfig(fmt::format(R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com:443
models:
  - name: file-key-model
    provider: openai
    model: gpt-4o
    api_key:
      filename: "{}"
  - name: inline-key-model
    provider: openai
    model: gpt-4o-mini
    api_key:
      inline_string: "\n  Bearer inline-sk-456\t "
routes:
  - requested_model: fast-model
    targets:
      - model: file-key-model
      - model: inline-key-model
)EOF",
                                                  key_file));
  ASSERT_TRUE(config_or.ok()) << config_or.status();

  const auto it = config_or.value()->routes().find("fast-model");
  ASSERT_NE(it, config_or.value()->routes().end());
  ASSERT_NE(it->second, nullptr);
  ASSERT_EQ(it->second->routes().size(), 2);
  EXPECT_EQ(it->second->routes()[0].api_key, "Bearer file-sk-123");
  EXPECT_EQ(it->second->routes()[1].api_key, "Bearer inline-sk-456");
}

TEST_F(ModelRouterConfigTest, FailsWhenApiKeyFileDoesNotExist) {
  const auto config_or = createConfig(fmt::format(
      R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com:443
models:
  - name: missing-file-model
    provider: openai
    model: gpt-4o
    api_key:
      filename: "{}"
routes:
  - requested_model: fast-model
    targets:
      - model: missing-file-model
)EOF",
      TestEnvironment::temporaryPath("nonexistent_model_router_api_key.txt")));
  EXPECT_FALSE(config_or.ok());
}

TEST_F(ModelRouterConfigTest, RejectsSemanticConfigErrors) {
  struct Case {
    std::string yaml;
    std::string expected_error;
  };
  const std::vector<Case> cases = {
      // Duplicate provider name.
      {R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: backup.openai.com
)EOF",
       "Duplicate provider name openai"},
      // Unspecified provider llm_protocol.
      {R"EOF(
providers:
  - name: unspecified-protocol-provider
    llm_protocol: LLM_PROTOCOL_UNSPECIFIED
    host_and_port: api.example.com
)EOF",
       "unspecified-protocol-provider"},
      // Invalid provider binding URI pattern.
      {R"EOF(
providers:
  - name: bad-binding-provider
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
    binding:
      uri_pattern: invalid_uri_pattern_without_leading_slash
)EOF",
       ""},
      // Duplicate model name.
      {R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
models:
  - name: gpt4o
    provider: openai
    model: gpt-4o
  - name: gpt4o
    provider: openai
    model: gpt-4o-mini
)EOF",
       "Duplicate model name gpt4o"},
      // Unknown model reference in route target.
      {R"EOF(
routes:
  - requested_model: fast-model
    targets:
      - model: nonexistent-model
)EOF",
       "Unknown model nonexistent-model"},
      // Unknown provider reference in model.
      {R"EOF(
models:
  - name: haiku
    provider: nonexistent-provider
    model: claude-haiku-4-5
routes:
  - requested_model: fast-model
    targets:
      - model: haiku
)EOF",
       "Unknown provider nonexistent-provider"},
      // Invalid host_and_port authority.
      {R"EOF(
providers:
  - name: bad-authority-provider
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: "api.openai.com/invalid/path"
models:
  - name: gpt4o
    provider: bad-authority-provider
    model: gpt-4o
routes:
  - requested_model: fast-model
    targets:
      - model: gpt4o
)EOF",
       "host_and_port value isn't valid Authority for provider bad-authority-provider"},
      // Invalid API key HTTP header value (contains newline inside the value).
      {R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
models:
  - name: bad-key-model
    provider: openai
    model: gpt-4o
    api_key:
      inline_string: "Bearer sk-123\ninjected: header"
routes:
  - requested_model: fast-model
    targets:
      - model: bad-key-model
)EOF",
       "API key value isn't valid HTTP header for model gpt-4o"},
      // Cross-provider models in the same route.
      {R"EOF(
providers:
  - name: anthropic
    llm_protocol: ANTHROPIC_MESSAGES
    host_and_port: api.anthropic.com
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
  - name: gpt4o
    provider: openai
    model: gpt-4o
routes:
  - requested_model: mixed-model
    targets:
      - model: haiku
      - model: gpt4o
)EOF",
       "All models in the route must have the same provider"},
      // Model quotas configured without quota_config.
      {R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
models:
  - name: gpt4o
    provider: openai
    model: gpt-4o
    quotas:
      - quota_type: REQUESTS
        time_unit: MINUTE
        limit: 10
)EOF",
       "quota_config must be specified when model quotas are configured"},
      // Unknown quota_config typed_config type URL.
      {R"EOF(
quota_config:
  name: unknown
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.RoutingTarget
    model: gpt4o
)EOF",
       "Unknown quota_config type:"},
  };

  for (const auto& c : cases) {
    const auto factory_cb = createFactoryFromYaml(c.yaml);
    ASSERT_FALSE(factory_cb.ok()) << c.yaml;
    EXPECT_EQ(factory_cb.status().code(), absl::StatusCode::kInvalidArgument);
    if (!c.expected_error.empty()) {
      EXPECT_THAT(std::string(factory_cb.status().message()), HasSubstr(c.expected_error));
    }
  }
}

TEST_F(ModelRouterConfigTest, RejectsProtoValidationViolations) {
  // Out-of-range LLMProtocol enum value.
  {
    ModelRouterProto proto;
    auto* provider = proto.add_providers();
    provider->set_name("bad-provider");
    provider->set_llm_protocol(static_cast<envoy::type::ai::v3::LLMProtocol>(999));
    provider->set_host_and_port("api.example.com");
    EXPECT_THROW_WITH_REGEX(createFactory(proto).IgnoreError(), EnvoyException,
                            "value must be one of the defined enum values");
  }

  struct YamlCase {
    std::string yaml;
    std::string expected_regex;
  };
  const std::vector<YamlCase> cases = {
      {R"EOF(
providers:
  - llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
)EOF",
       "ProviderValidationError\\.Name:"},
      {R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
)EOF",
       "ProviderValidationError\\.HostAndPort:"},
      {R"EOF(
models:
  - provider: openai
    model: gpt-4o
)EOF",
       "ModelValidationError\\.Name:"},
      {R"EOF(
models:
  - name: gpt4o
    model: gpt-4o
)EOF",
       "ModelValidationError\\.Provider:"},
      {R"EOF(
models:
  - name: gpt4o
    provider: openai
)EOF",
       "ModelValidationError\\.Model:"},
      {R"EOF(
models:
  - name: empty-datasource-model
    provider: openai
    model: gpt-4o
    api_key: {}
)EOF",
       "ModelValidationError\\.ApiKey:.*field: \"specifier\", reason: is required"},
      {R"EOF(
routes:
  - targets:
      - model: gpt4o
)EOF",
       "RoutingEntryValidationError\\.RequestedModel:"},
      {R"EOF(
routes:
  - requested_model: fast-model
)EOF",
       "RoutingEntryValidationError\\.Targets:"},
      {R"EOF(
routes:
  - requested_model: fast-model
    targets:
      - {}
)EOF",
       "RoutingTargetValidationError\\.Model:"},
      {R"EOF(
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    quotas:
      - quota_type: QUOTA_UNSPECIFIED
        time_unit: MINUTE
        limit: 10
)EOF",
       "QuotaValidationError\\.QuotaType"},
      {R"EOF(
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    quotas:
      - quota_type: REQUESTS
        time_unit: TIME_UNSPECIFIED
        limit: 10
)EOF",
       "QuotaValidationError\\.TimeUnit"},
      {R"EOF(
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    quotas:
      - quota_type: REQUESTS
        time_unit: HOUR
        limit: 10
        unit_multiplier: 0
)EOF",
       "QuotaValidationError\\.UnitMultiplier"},
      {R"EOF(
quota_config: {}
)EOF",
       "TypedExtensionConfigValidationError"},
      {R"EOF(
quota_config:
  name: redis
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.RedisQuotaConfig
    host: ""
)EOF",
       "RedisQuotaConfigValidationError\\.Host"},
      {R"EOF(
quota_config:
  name: redis
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.RedisQuotaConfig
    host: 127.0.0.1
    port: 70000
)EOF",
       "RedisQuotaConfigValidationError\\.Port"},
      {R"EOF(
quota_config:
  name: redis
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.RedisQuotaConfig
    host: 127.0.0.1
    timeout: 0s
)EOF",
       "RedisQuotaConfigValidationError\\.Timeout"},
  };

  for (const auto& c : cases) {
    EXPECT_THROW_WITH_REGEX(createFactoryFromYaml(c.yaml).IgnoreError(), EnvoyException,
                            c.expected_regex);
  }
}

TEST_F(ModelRouterConfigTest, AcceptsValidQuotasAndRedisConfig) {
  for (const char* yaml : {
           R"EOF(
providers:
  - name: openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: api.openai.com
models:
  - name: gpt4o
    provider: openai
    model: gpt-4o
    quotas:
      - quota_type: REQUESTS
        time_unit: MINUTE
        limit: 0
      - quota_type: TOKENS
        time_unit: HOUR
        limit: 10000
        unit_multiplier: 6
      - quota_type: REQUESTS
        time_unit: DAY
        limit: 500
routes:
  - requested_model: chat
    targets:
      - model: gpt4o
quota_config:
  name: local
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.LocalQuotaConfig
)EOF",
           R"EOF(
quota_config:
  name: redis
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.RedisQuotaConfig
    host: 127.0.0.1
    port: 6379
    timeout: 2s
)EOF",
       }) {
    const auto factory_cb = createFactoryFromYaml(yaml);
    ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();
  }
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
