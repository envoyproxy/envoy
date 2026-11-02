#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/http/ai_filters/route_selector/v3/route_selector.pb.h"
#include "envoy/type/ai/v3/llm_binding.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"
#include "envoy/type/ai/v3/routes.pb.h"

#include "source/common/http/headers.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/http/ai_filters/model_router/route_wrapper.h"
#include "source/extensions/http/ai_filters/route_selector/filter.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LlmBinding;
using HttpFilters::AiProtocolManager::LlmBindingObject;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmProtocolFilterState;
using HttpFilters::AiProtocolManager::stringFromFilterState;
namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using RouteSelectorProto = envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector;

class RouteSelectorFilterTest : public testing::Test {
public:
  RouteSelectorFilterTest()
      : api_(Api::createApiForTest()),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain),
        config_(std::make_shared<const RouteSelectorFilterConfig>(RouteSelectorProto{},
                                                                  *stats_store_.rootScope())) {}

  void setRoutesInFilterState(const envoy::type::ai::v3::LlmRoutes& routes) {
    ModelRouter::LlmRoutesFilterStateObject::Bindings bindings;
    bindings.reserve(routes.routes_size());
    for (const auto& route : routes.routes()) {
      if (route.has_binding()) {
        auto parsed = LlmBinding::create(route.binding());
        ASSERT_TRUE(parsed.ok()) << parsed.status();
        bindings.push_back(*std::move(parsed));
      } else {
        bindings.push_back(nullptr);
      }
    }
    stream_info_.filterState()->setData(
        FilterStateKeys::ResolvedRoutes,
        std::make_shared<ModelRouter::LlmRoutesFilterStateObject>(routes, std::move(bindings)),
        StreamInfo::FilterState::LifeSpan::Request);
  }

  void populateRoutesInFilterState() {
    envoy::type::ai::v3::LlmRoutes routes;
    TestUtility::loadFromYaml(R"EOF(
routes:
  - llm_protocol: OPENAI_CHAT_COMPLETIONS
    model: gpt-4o
    host_and_port: api.openai.com
    binding:
      uri_pattern: /v1/chat/completions
    api_key: "Bearer sk-openai-1"
  - llm_protocol: OPENAI_RESPONSES
    model: gpt-4.1
    host_and_port: api.openai.com
  - llm_protocol: ANTHROPIC_MESSAGES
    model: claude-haiku-4-5
    host_and_port: api.anthropic.com
    binding:
      uri_pattern: /v1/messages
    api_key: "Bearer sk-anthropic"
  - llm_protocol: GEMINI_GENERATE_CONTENT
    model: gemini-2.5-pro
    host_and_port: generativelanguage.googleapis.com:443
    binding:
      uri_pattern: /v1beta/models/{model}:{generateContent|streamGenerateContent}
    api_key: "gemini-key-123"
  - llm_protocol: LLM_PROTOCOL_UNSPECIFIED
    model: custom-model
    host_and_port: custom.llm.local
)EOF",
                              routes);
    setRoutesInFilterState(routes);
  }

  void run(std::optional<uint32_t> attempt_count = 1,
           const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    reply_code_.reset();
    reply_details_.clear();
    if (attempt_count.has_value()) {
      stream_info_.setAttemptCount(*attempt_count);
    }

    request_headers_ = Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/v1/chat/completions"}, {":authority", "original.host"}};
    for (const auto& [key, val] : extra_headers) {
      request_headers_.addCopy(Http::LowerCaseString(key), val);
    }

    const AiFilterContext context{stream_info_, request_headers_,
                                  LLMProtocol::OpenAiChatCompletions};
    RouteSelectorFilter filter(config_, context);
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

  const LlmProtocolFilterState* responseProtocolObject() {
    return stream_info_.filterState()->getDataReadOnly<LlmProtocolFilterState>(
        FilterStateKeys::LlmProtocolResponse);
  }

  std::optional<std::string> resolvedModel() {
    const auto val =
        stringFromFilterState(*stream_info_.filterState(), FilterStateKeys::ModelResolved);
    return val.has_value() ? std::make_optional(std::string(*val)) : std::nullopt;
  }

  const LlmBindingObject* responseBindingObject() {
    return stream_info_.filterState()->getDataReadOnly<LlmBindingObject>(
        FilterStateKeys::BindingResponse);
  }

  void expectNoOutputFilterState() {
    EXPECT_EQ(responseProtocolObject(), nullptr);
    EXPECT_FALSE(resolvedModel().has_value());
    EXPECT_EQ(responseBindingObject(), nullptr);
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter =
        TestUtility::findCounter(stats_store_, "ai_protocol_manager.route_selector." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  Api::ApiPtr api_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  RouteSelectorFilterConfigSharedPtr config_;
  Http::TestRequestHeaderMapImpl request_headers_;
  std::optional<Http::Code> reply_code_;
  std::string reply_details_;
};

TEST_F(RouteSelectorFilterTest, MissingResolvedRoutesInFilterState) {
  run(1);

  expectLocalReply(Http::Code::InternalServerError, "LLM routes not found");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(RouteSelectorFilterTest, MissingOrInvalidAttemptCount) {
  populateRoutesInFilterState();

  // Missing attempt count in stream_info.
  run(std::nullopt);
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 1);

  // Zero attempt count (1-based index required).
  run(0);
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 2);

  // Attempt count out of bounds (5 routes configured, attempt 6 requested).
  run(6);
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 3);
}

TEST_F(RouteSelectorFilterTest, RejectsUnspecifiedProtocol) {
  populateRoutesInFilterState();
  run(5, {{"authorization", "Bearer client-token"}});

  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM protocol");
  expectNoOutputFilterState();
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer client-token");
  EXPECT_EQ(counterValue("invalid_route_data"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(RouteSelectorFilterTest, SelectsRoutesAndUpdatesHeadersAndFilterStateAcrossAttempts) {
  populateRoutesInFilterState();
  const std::vector<std::pair<std::string, std::string>> stale_headers = {
      {"authorization", "Bearer stale-client-token"},
      {"x-goog-api-key", "stale-google-key"},
      {"anthropic-version", "2023-01-01"},
  };

  // Attempt 1: OpenAI Chat Completions with binding and api_key.
  run(1, stale_headers);
  EXPECT_FALSE(reply_code_.has_value());
  ASSERT_NE(responseProtocolObject(), nullptr);
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(resolvedModel(), "gpt-4o");
  ASSERT_NE(responseBindingObject(), nullptr);
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(), "/v1/chat/completions");
  EXPECT_EQ(request_headers_.getHostValue(), "api.openai.com");
  ASSERT_EQ(request_headers_.get(Http::CustomHeaders::get().Authorization).size(), 1);
  EXPECT_EQ(
      request_headers_.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Bearer sk-openai-1");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  // Attempt 2: OpenAI Responses without binding or api_key clears BindingResponse and sets empty
  // Authorization so client credentials are not leaked.
  run(2, stale_headers);
  EXPECT_FALSE(reply_code_.has_value());
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::OpenAiResponses);
  EXPECT_EQ(resolvedModel(), "gpt-4.1");
  EXPECT_EQ(responseBindingObject(), nullptr);
  const auto cleared_binding = LlmBindingObject::fromFilterState(*stream_info_.filterState(),
                                                                 FilterStateKeys::BindingResponse);
  ASSERT_TRUE(cleared_binding.ok());
  EXPECT_EQ(*cleared_binding, nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "api.openai.com");
  ASSERT_EQ(request_headers_.get(Http::CustomHeaders::get().Authorization).size(), 1);
  EXPECT_EQ(
      request_headers_.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  // Attempt 3: Anthropic Messages sets anthropic-version, Authorization, and binding.
  run(3, stale_headers);
  EXPECT_FALSE(reply_code_.has_value());
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(resolvedModel(), "claude-haiku-4-5");
  ASSERT_NE(responseBindingObject(), nullptr);
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(), "/v1/messages");
  EXPECT_EQ(request_headers_.getHostValue(), "api.anthropic.com");
  ASSERT_EQ(request_headers_.get(Http::LowerCaseString("anthropic-version")).size(), 1);
  EXPECT_EQ(
      request_headers_.get(Http::LowerCaseString("anthropic-version"))[0]->value().getStringView(),
      "2023-06-01");
  ASSERT_EQ(request_headers_.get(Http::CustomHeaders::get().Authorization).size(), 1);
  EXPECT_EQ(
      request_headers_.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Bearer sk-anthropic");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());

  // Attempt 4: Gemini Generate Content sets x-goog-api-key and strips Authorization and
  // anthropic-version.
  run(4, stale_headers);
  EXPECT_FALSE(reply_code_.has_value());
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(resolvedModel(), "gemini-2.5-pro");
  ASSERT_NE(responseBindingObject(), nullptr);
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(),
            "/v1beta/models/{model}:{generateContent|streamGenerateContent}");
  EXPECT_EQ(request_headers_.getHostValue(), "generativelanguage.googleapis.com:443");
  ASSERT_EQ(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).size(), 1);
  EXPECT_EQ(
      request_headers_.get(Http::LowerCaseString("x-goog-api-key"))[0]->value().getStringView(),
      "gemini-key-123");
  EXPECT_TRUE(request_headers_.get(Http::CustomHeaders::get().Authorization).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  EXPECT_EQ(counterValue("success"), 4);
  EXPECT_EQ(counterValue("invalid_route_data"), 0);
}

} // namespace
} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
