#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmRoutesFilterStateObject;
namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

class ModelRouterFilterTest : public testing::Test {
public:
  ModelRouterFilterTest()
      : api_(Api::createApiForTest()),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ModelRouterFilterConfigSharedPtr defaultConfig() {
    ModelRouterProto proto;
    TestUtility::loadFromYaml(R"EOF(
providers:
  - name: anthropic
    llm_protocol: ANTHROPIC_MESSAGES
    host_and_port: api.anthropic.com
    binding:
      uri_pattern: /v1/messages
  - name: primary_openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: primary.llm.example.com:8443
    binding:
      uri_pattern: /v1/chat/completions
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    api_key:
      inline_string: "Bearer sk-ant"
  - name: gpt4o
    provider: primary_openai
    model: gpt-4o
    api_key:
      inline_string: "Bearer sk-oai"
  - name: gpt4o_mini
    provider: primary_openai
    model: gpt-4o-mini
    api_key:
      inline_string: "Bearer sk-mini"
  - name: gpt4o_nano
    provider: primary_openai
    model: gpt-4o-nano
    api_key:
      inline_string: "Bearer sk-nano"
routes:
  - requested_model: send_me_to_anthropic
    targets:
      - model: haiku
  - requested_model: multi_fallback_model
    targets:
      - model: gpt4o
      - model: gpt4o_mini
      - model: gpt4o_nano
)EOF",
                              proto);
    auto config_or = ModelRouterFilterConfig::create(proto, *api_, *stats_store_.rootScope());
    EXPECT_TRUE(config_or.ok()) << config_or.status();
    return *std::move(config_or);
  }

  void setModelInFilterState(absl::string_view model_name) {
    stream_info_.filterState()->setData(FilterStateKeys::ModelRequest,
                                        std::make_shared<Router::StringAccessorImpl>(model_name),
                                        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  void run(ModelRouterFilterConfigSharedPtr config = nullptr) {
    reply_code_.reset();
    reply_details_.clear();
    request_headers_ = Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/v1/chat/completions"}, {":authority", "original.host"}};

    const AiFilterContext context{stream_info_, request_headers_,
                                  LLMProtocol::OpenAiChatCompletions};
    ModelRouterFilter filter(config != nullptr ? std::move(config) : defaultConfig(), context);
    AiRequest request{JsonWithExtBuf{}};
    const absl::Status status =
        filter.decodeSync(request, [this](Http::Code code, std::string details) {
          reply_code_ = code;
          reply_details_ = std::move(details);
        });
    EXPECT_TRUE(status.ok()) << status;
  }

  void expectLocalReply(Http::Code expected_code, absl::string_view expected_details) {
    EXPECT_EQ(reply_code_, expected_code);
    EXPECT_EQ(reply_details_, expected_details);
  }

  const LlmRoutesFilterStateObject* resolvedRoutes() {
    return stream_info_.filterState()->getDataReadOnly<LlmRoutesFilterStateObject>(
        FilterStateKeys::ResolvedRoutes);
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter =
        TestUtility::findCounter(stats_store_, "ai_protocol_manager.model_router." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  Api::ApiPtr api_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Http::TestRequestHeaderMapImpl request_headers_;
  std::optional<Http::Code> reply_code_;
  std::string reply_details_;
};

TEST_F(ModelRouterFilterTest, ModelNameMissingInFilterState) {
  run();
  expectLocalReply(Http::Code::NotFound, "Model name not found in filter state");

  EXPECT_EQ(resolvedRoutes(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  EXPECT_TRUE(request_headers_.getEnvoyMaxRetriesValue().empty());

  EXPECT_EQ(counterValue("model_name_missing"), 1);
  EXPECT_EQ(counterValue("route_not_found"), 0);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(ModelRouterFilterTest, RouteNotFoundForModel) {
  setModelInFilterState("unknown_model");
  run();
  expectLocalReply(Http::Code::NotFound, "Unknown model");

  EXPECT_EQ(resolvedRoutes(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  EXPECT_TRUE(request_headers_.getEnvoyMaxRetriesValue().empty());

  EXPECT_EQ(counterValue("model_name_missing"), 0);
  EXPECT_EQ(counterValue("route_not_found"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(ModelRouterFilterTest, SingleTargetRouteResolved) {
  setModelInFilterState("send_me_to_anthropic");
  run();
  EXPECT_FALSE(reply_code_.has_value());

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 1);
  EXPECT_EQ(routes_obj->routes()[0].model, "claude-haiku-4-5");
  ASSERT_NE(routes_obj->routes()[0].binding, nullptr);
  EXPECT_EQ(routes_obj->routes()[0].binding->pattern()->source(), "/v1/messages");

  EXPECT_EQ(request_headers_.getHostValue(), "api.anthropic.com");
  EXPECT_EQ(request_headers_.getEnvoyMaxRetriesValue(), "0");

  EXPECT_EQ(counterValue("model_name_missing"), 0);
  EXPECT_EQ(counterValue("route_not_found"), 0);
  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(ModelRouterFilterTest, MultipleTargetsRouteSetsMaxRetriesAndPrimaryAuthority) {
  setModelInFilterState("multi_fallback_model");
  run();
  EXPECT_FALSE(reply_code_.has_value());

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 3);
  EXPECT_EQ(routes_obj->routes()[0].model, "gpt-4o");
  EXPECT_EQ(routes_obj->routes()[1].model, "gpt-4o-mini");
  EXPECT_EQ(routes_obj->routes()[2].model, "gpt-4o-nano");

  ASSERT_NE(routes_obj->routes()[0].binding, nullptr);
  ASSERT_NE(routes_obj->routes()[1].binding, nullptr);
  ASSERT_NE(routes_obj->routes()[2].binding, nullptr);
  EXPECT_EQ(routes_obj->routes()[0].binding->pattern()->source(), "/v1/chat/completions");
  EXPECT_EQ(routes_obj->routes()[1].binding->pattern()->source(), "/v1/chat/completions");
  EXPECT_EQ(routes_obj->routes()[2].binding->pattern()->source(), "/v1/chat/completions");

  EXPECT_EQ(request_headers_.getHostValue(), "primary.llm.example.com:8443");
  EXPECT_EQ(request_headers_.getEnvoyMaxRetriesValue(), "2");
  EXPECT_EQ(counterValue("success"), 1);
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
