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
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/model_router/route_wrapper.h"
#include "source/extensions/http/ai_filters/route_selector/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RouteSelector {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LlmBindingObject;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmProtocolFilterState;
using HttpFilters::AiProtocolManager::stringFromFilterState;
namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using RouteSelectorProto = envoy::extensions::http::ai_filters::route_selector::v3::RouteSelector;

class RouteSelectorFilterTest : public testing::Test {
public:
  RouteSelectorFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~RouteSelectorFilterTest() override { buffer_manager_.onDestroy(); }

  void setRoutesInFilterState(const envoy::type::ai::v3::LlmRoutes& routes) {
    stream_info_.filterState()->setData(
        FilterStateKeys::ResolvedRoutes,
        std::make_shared<ModelRouter::LlmRoutesFilterStateObject>(routes),
        StreamInfo::FilterState::LifeSpan::Request);
  }

  void populateRoutesInFilterState() {
    envoy::type::ai::v3::LlmRoutes routes;

    auto* r1 = routes.add_routes();
    r1->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
    r1->set_model("gpt-4o");
    r1->set_host_and_port("api.openai.com");
    r1->mutable_binding()->set_uri_pattern("/v1/chat/completions");
    r1->set_api_key("Bearer sk-openai-1");

    auto* r2 = routes.add_routes();
    r2->set_llm_protocol(envoy::type::ai::v3::OPENAI_RESPONSES);
    r2->set_model("gpt-4.1");

    auto* r3 = routes.add_routes();
    r3->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
    r3->set_model("claude-haiku-4-5");
    r3->set_host_and_port("api.anthropic.com");
    r3->mutable_binding()->set_uri_pattern("/v1/messages");
    r3->set_api_key("Bearer sk-anthropic");

    auto* r4 = routes.add_routes();
    r4->set_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
    r4->set_model("gemini-2.5-pro");
    r4->set_host_and_port("generativelanguage.googleapis.com:443");
    r4->mutable_binding()->set_uri_pattern(
        "/v1beta/models/{model}:{generateContent|streamGenerateContent}");
    r4->set_api_key("gemini-key-123");

    auto* r5 = routes.add_routes();
    r5->set_llm_protocol(envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED);
    r5->set_model("custom-model");
    r5->set_host_and_port("custom.llm.local");

    auto* r6 = routes.add_routes();
    r6->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
    r6->set_model("gpt-4o");
    r6->set_host_and_port("api.openai.com");
    r6->mutable_binding()->set_uri_pattern("invalid_uri_pattern_without_leading_slash");

    setRoutesInFilterState(routes);
  }

  void run(std::optional<std::string> attempt_count = "1",
           const std::string& payload = R"({"model":"requested","messages":[]})",
           const std::vector<std::pair<std::string, std::string>>& extra_headers = {}) {
    status_.reset();
    reply_code_.reset();
    reply_details_.clear();
    bridge_.injected_.drain(bridge_.injected_.length());

    request_headers_ = Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/v1/chat/completions"}, {":authority", "original.host"}};
    for (const auto& [key, val] : extra_headers) {
      request_headers_.addCopy(Http::LowerCaseString(key), val);
    }
    if (attempt_count.has_value()) {
      request_headers_.setEnvoyAttemptCount(*attempt_count);
    }

    JsonWithExtBuf doc;
    doc.setJson(nlohmann::json::parse(payload));

    RouteSelectorProto proto;
    auto config =
        std::make_shared<const RouteSelectorFilterConfig>(proto, *stats_store_.rootScope());
    const AiFilterContext context{stream_info_, request_headers_,
                                  LLMProtocol::OpenAiChatCompletions, payload.size()};
    std::vector<AiFilterSharedPtr> filters;
    filters.push_back(std::make_shared<RouteSelectorFilter>(std::move(config), context));
    FilterManager manager(std::move(filters));

    manager.startRequest(
        std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
        [this](absl::Status s) { status_ = std::move(s); }, &request_headers_,
        [this](Http::Code code, std::string details) {
          reply_code_ = code;
          reply_details_ = std::move(details);
        },
        /*always_serialize=*/true, LLMProtocol::OpenAiChatCompletions);
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    ASSERT_TRUE(status_.has_value());
  }

  void
  expectForwarded(absl::string_view expected_payload = R"({"model":"requested","messages":[]})") {
    EXPECT_TRUE(status_->ok()) << *status_;
    EXPECT_FALSE(reply_code_.has_value());
    EXPECT_EQ(nlohmann::json::parse(bridge_.injected_.toString()),
              nlohmann::json::parse(expected_payload));
  }

  void expectLocalReply(Http::Code expected_code, absl::string_view expected_details) {
    EXPECT_TRUE(absl::IsCancelled(*status_)) << *status_;
    EXPECT_EQ(reply_code_, expected_code);
    EXPECT_EQ(reply_details_, expected_details);
    EXPECT_EQ(bridge_.injected_.length(), 0);
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
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge bridge_;
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Http::TestRequestHeaderMapImpl request_headers_;

  std::optional<absl::Status> status_;
  std::optional<Http::Code> reply_code_;
  std::string reply_details_;
};

TEST_F(RouteSelectorFilterTest, MissingResolvedRoutesInFilterState) {
  run("1");

  expectLocalReply(Http::Code::InternalServerError, "LLM routes not found");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(RouteSelectorFilterTest, MissingOrInvalidAttemptCountHeader) {
  populateRoutesInFilterState();

  // Missing x-envoy-attempt-count header.
  run(std::nullopt);
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 1);

  // Non-numeric x-envoy-attempt-count header.
  run("abc");
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 2);

  // Zero attempt count (1-based index required).
  run("0");
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 3);

  // Negative attempt count.
  run("-1");
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 4);

  // Attempt count out of bounds (6 routes configured, attempt 7 requested).
  run("7");
  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM request attempt");
  expectNoOutputFilterState();
  EXPECT_EQ(counterValue("invalid_route_data"), 5);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(RouteSelectorFilterTest, SelectsOpenAiChatCompletionsOnFirstAttempt) {
  populateRoutesInFilterState();
  run("1");
  expectForwarded();

  ASSERT_NE(responseProtocolObject(), nullptr);
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(resolvedModel(), "gpt-4o");
  ASSERT_NE(responseBindingObject(), nullptr);
  ASSERT_TRUE(responseBindingObject()->binding().ok());
  ASSERT_TRUE((*responseBindingObject()->binding())->pattern().has_value());
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(), "/v1/chat/completions");

  EXPECT_EQ(request_headers_.getHostValue(), "api.openai.com");
  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer sk-openai-1");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  EXPECT_EQ(counterValue("success"), 1);
  EXPECT_EQ(counterValue("invalid_route_data"), 0);
}

TEST_F(RouteSelectorFilterTest, SelectsOpenAiResponsesWithoutOptionalFieldsOnSecondAttempt) {
  populateRoutesInFilterState();
  run("2");
  expectForwarded();

  ASSERT_NE(responseProtocolObject(), nullptr);
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::OpenAiResponses);
  EXPECT_EQ(resolvedModel(), "gpt-4.1");
  EXPECT_EQ(responseBindingObject(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "");
  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(RouteSelectorFilterTest, SelectsAnthropicMessagesAndAddsVersionHeaderOnThirdAttempt) {
  populateRoutesInFilterState();
  run("3");
  expectForwarded();

  ASSERT_NE(responseProtocolObject(), nullptr);
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(resolvedModel(), "claude-haiku-4-5");
  ASSERT_NE(responseBindingObject(), nullptr);
  ASSERT_TRUE(responseBindingObject()->binding().ok());
  ASSERT_TRUE((*responseBindingObject()->binding())->pattern().has_value());
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(), "/v1/messages");

  EXPECT_EQ(request_headers_.getHostValue(), "api.anthropic.com");
  const auto anthropic_version = request_headers_.get(Http::LowerCaseString("anthropic-version"));
  ASSERT_EQ(anthropic_version.size(), 1);
  EXPECT_EQ(anthropic_version[0]->value().getStringView(), "2023-06-01");

  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer sk-anthropic");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());

  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(RouteSelectorFilterTest, SelectsGeminiGenerateContentOnFourthAttempt) {
  populateRoutesInFilterState();
  run("4");
  expectForwarded();

  ASSERT_NE(responseProtocolObject(), nullptr);
  EXPECT_EQ(responseProtocolObject()->protocol(), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(resolvedModel(), "gemini-2.5-pro");
  ASSERT_NE(responseBindingObject(), nullptr);
  ASSERT_TRUE(responseBindingObject()->binding().ok());
  ASSERT_TRUE((*responseBindingObject()->binding())->pattern().has_value());
  EXPECT_EQ((*responseBindingObject()->binding())->pattern()->source(),
            "/v1beta/models/{model}:{generateContent|streamGenerateContent}");

  EXPECT_EQ(request_headers_.getHostValue(), "generativelanguage.googleapis.com:443");
  const auto google_key = request_headers_.get(Http::LowerCaseString("x-goog-api-key"));
  ASSERT_EQ(google_key.size(), 1);
  EXPECT_EQ(google_key[0]->value().getStringView(), "gemini-key-123");
  EXPECT_TRUE(request_headers_.get(Http::CustomHeaders::get().Authorization).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(RouteSelectorFilterTest, CleansUpPreExistingManagedHeaders) {
  envoy::type::ai::v3::LlmRoutes routes;

  // Attempt 1: Gemini replaces x-goog-api-key and strips Authorization / anthropic-version.
  auto* gemini_with_key = routes.add_routes();
  gemini_with_key->set_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  gemini_with_key->set_model("gemini-2.5-flash");
  gemini_with_key->set_host_and_port("generativelanguage.googleapis.com");
  gemini_with_key->set_api_key("new-gemini-key");

  // Attempt 2: Gemini without api_key or host_and_port sets x-goog-api-key and :authority to ""
  // and strips Authorization / anthropic-version.
  auto* gemini_no_key = routes.add_routes();
  gemini_no_key->set_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  gemini_no_key->set_model("gemini-2.5-flash");

  // Attempt 3: Anthropic strips x-goog-api-key and sets anthropic-version, Authorization, and
  // :authority.
  auto* anthropic_no_key = routes.add_routes();
  anthropic_no_key->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  anthropic_no_key->set_model("claude-sonnet-4-5");
  anthropic_no_key->set_host_and_port("api.anthropic.com");

  // Attempt 4: OpenAI strips x-goog-api-key / anthropic-version and replaces Authorization and
  // :authority.
  auto* openai_with_key = routes.add_routes();
  openai_with_key->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  openai_with_key->set_model("gpt-4o-mini");
  openai_with_key->set_host_and_port("api.openai.com");
  openai_with_key->set_api_key("Bearer new-openai-key");

  setRoutesInFilterState(routes);

  const std::vector<std::pair<std::string, std::string>> stale_headers = {
      {"authorization", "Bearer stale-client-token"},
      {"x-goog-api-key", "stale-google-key"},
      {"anthropic-version", "2023-01-01"},
  };

  run("1", R"({"model":"requested","messages":[]})", stale_headers);
  expectForwarded();
  EXPECT_EQ(request_headers_.getHostValue(), "generativelanguage.googleapis.com");
  EXPECT_TRUE(request_headers_.get(Http::CustomHeaders::get().Authorization).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());
  auto google_key = request_headers_.get(Http::LowerCaseString("x-goog-api-key"));
  ASSERT_EQ(google_key.size(), 1);
  EXPECT_EQ(google_key[0]->value().getStringView(), "new-gemini-key");

  run("2", R"({"model":"requested","messages":[]})", stale_headers);
  expectForwarded();
  EXPECT_EQ(request_headers_.getHostValue(), "");
  EXPECT_TRUE(request_headers_.get(Http::CustomHeaders::get().Authorization).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());
  google_key = request_headers_.get(Http::LowerCaseString("x-goog-api-key"));
  ASSERT_EQ(google_key.size(), 1);
  EXPECT_EQ(google_key[0]->value().getStringView(), "");

  run("3", R"({"model":"requested","messages":[]})", stale_headers);
  expectForwarded();
  EXPECT_EQ(request_headers_.getHostValue(), "api.anthropic.com");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "");
  auto anthropic_version = request_headers_.get(Http::LowerCaseString("anthropic-version"));
  ASSERT_EQ(anthropic_version.size(), 1);
  EXPECT_EQ(anthropic_version[0]->value().getStringView(), "2023-06-01");

  run("4", R"({"model":"requested","messages":[]})", stale_headers);
  expectForwarded();
  EXPECT_EQ(request_headers_.getHostValue(), "api.openai.com");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());
  auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer new-openai-key");
}

TEST_F(RouteSelectorFilterTest, RejectsUnspecifiedProtocolOnFifthAttempt) {
  populateRoutesInFilterState();
  run("5", R"({"model":"requested","messages":[]})", {{"authorization", "Bearer client-token"}});

  expectLocalReply(Http::Code::InternalServerError, "Invalid LLM protocol");
  expectNoOutputFilterState();
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer client-token");
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString("anthropic-version")).empty());

  EXPECT_EQ(counterValue("invalid_route_data"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(RouteSelectorFilterTest, RejectsInvalidLlmBindingOnSixthAttempt) {
  populateRoutesInFilterState();
  run("6", R"({"model":"requested","messages":[]})", {{"authorization", "Bearer client-token"}});

  EXPECT_TRUE(absl::IsCancelled(*status_)) << *status_;
  EXPECT_EQ(reply_code_, Http::Code::InternalServerError);
  EXPECT_THAT(reply_details_, testing::StartsWith("Invalid LLM binding: "));
  EXPECT_EQ(bridge_.injected_.length(), 0);
  expectNoOutputFilterState();
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  const auto auth = request_headers_.get(Http::CustomHeaders::get().Authorization);
  ASSERT_EQ(auth.size(), 1);
  EXPECT_EQ(auth[0]->value().getStringView(), "Bearer client-token");

  EXPECT_EQ(counterValue("invalid_route_data"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

} // namespace
} // namespace RouteSelector
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
