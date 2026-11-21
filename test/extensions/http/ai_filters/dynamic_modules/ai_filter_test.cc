#include <memory>
#include <optional>
#include <string>

#include "envoy/router/string_accessor.h"

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/dynamic_modules/ai_filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using Extensions::DynamicModules::DynamicModulePtr;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using StatusHelpers::HasStatusCode;
using testing::HasSubstr;
using testing::NiceMock;

DynamicModulePtr loadModule(absl::string_view name) {
  auto module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath(std::string(name), "c"),
      /*do_not_close=*/true);
  EXPECT_OK(module.status());
  return std::move(module).value();
}

AiRequest chatRequest(LLMProtocol protocol = LLMProtocol::OpenAiChatCompletions) {
  JsonWithExtBuf document;
  document.setJson(nlohmann::json::parse(R"({
    "model": "gpt-4o",
    "temperature": 0.5,
    "messages": [{"role": "user", "content": null}]
  })"));
  document.json()["messages"][0]["content"] =
      JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{60, 4096});
  return AiRequest(std::move(document), protocol);
}

class DynamicModuleAiFilterTest : public testing::Test {
protected:
  DynamicModuleAiFilterTest() : counters_(loadModule("ai_filter")) {}

  ~DynamicModuleAiFilterTest() override {
    EXPECT_EQ(0, liveCount("ai_filter_test_live_filters"));
    EXPECT_EQ(0, liveCount("ai_filter_test_live_configs"));
  }

  DynamicModuleAiFilterConfigSharedPtr config(absl::string_view scenario) {
    auto config =
        DynamicModuleAiFilterConfig::create(scenario, "", loadModule("ai_filter"), dispatcher_);
    EXPECT_OK(config.status());
    return std::move(config).value();
  }

  std::shared_ptr<DynamicModuleAiFilter> filter(absl::string_view scenario) {
    return DynamicModuleAiFilter::create(
        config(scenario), AiFilterContext{stream_info_, headers_, LLMProtocol::Unspecified});
  }

  absl::Status run(DynamicModuleAiFilter& filter, AiRequest& request) {
    return filter.decodeSync(request, [this](Http::Code code, std::string body) {
      reply_code_ = code;
      reply_body_ = std::move(body);
    });
  }

  std::string filterState(absl::string_view key) {
    const auto* accessor = stream_info_.filterState()->getDataReadOnly<Router::StringAccessor>(key);
    return accessor == nullptr ? "" : std::string(accessor->asString());
  }

  int liveCount(absl::string_view symbol) {
    auto count = counters_->getFunctionPointer<int (*)()>(symbol);
    EXPECT_OK(count.status());
    return (*count.value())();
  }

  const DynamicModulePtr counters_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl headers_{{":method", "POST"}, {":path", "/v1/chat/completions"}};
  std::optional<Http::Code> reply_code_;
  std::string reply_body_;
};

TEST_F(DynamicModuleAiFilterTest, MissingHookIsNotFound) {
  auto config = DynamicModuleAiFilterConfig::create(
      "x", "", loadModule("ai_filter_missing_request"), dispatcher_);
  EXPECT_THAT(config, HasStatusCode(absl::StatusCode::kNotFound));
  EXPECT_THAT(std::string(config.status().message()),
              HasSubstr("envoy_dynamic_module_on_ai_filter_request"));
}

TEST_F(DynamicModuleAiFilterTest, RejectedConfigIsInvalidArgument) {
  auto config =
      DynamicModuleAiFilterConfig::create("unknown", "", loadModule("ai_filter"), dispatcher_);
  EXPECT_THAT(config, HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(0, liveCount("ai_filter_test_live_configs"));
}

TEST_F(DynamicModuleAiFilterTest, ConfigIsDestroyedOnTheMainThreadDispatcher) {
  auto passthrough = config("passthrough");
  EXPECT_EQ(1, liveCount("ai_filter_test_live_configs"));
  EXPECT_CALL(dispatcher_, deleteInDispatcherThread(testing::NotNull()));
  passthrough.reset();
  EXPECT_EQ(0, liveCount("ai_filter_test_live_configs"));
}

TEST_F(DynamicModuleAiFilterTest, PassthroughContinues) {
  auto passthrough = filter("passthrough");
  ASSERT_NE(passthrough, nullptr);
  EXPECT_EQ(1, liveCount("ai_filter_test_live_filters"));
  AiRequest request = chatRequest();
  EXPECT_OK(run(*passthrough, request));
  EXPECT_EQ(request.json()["model"], "gpt-4o");
  EXPECT_FALSE(reply_code_.has_value());
}

TEST_F(DynamicModuleAiFilterTest, ModuleSkipsStream) {
  EXPECT_NE(filter("skip_on_header"), nullptr);
  headers_.addCopy("x-skip", "1");
  EXPECT_EQ(filter("skip_on_header"), nullptr);
}

TEST_F(DynamicModuleAiFilterTest, FailedCreationFailsTheRequest) {
  for (absl::string_view scenario : {"fail_new", "null_filter"}) {
    SCOPED_TRACE(scenario);
    auto failed = filter(scenario);
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(0, liveCount("ai_filter_test_live_filters"));
    AiRequest request = chatRequest();
    EXPECT_THAT(run(*failed, request), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_FALSE(reply_code_.has_value());
  }
}

TEST_F(DynamicModuleAiFilterTest, ErrorFailsRequest) {
  auto error = filter("error");
  AiRequest request = chatRequest();
  EXPECT_THAT(run(*error, request), HasStatusCode(absl::StatusCode::kInternal));
}

TEST_F(DynamicModuleAiFilterTest, LocalReplyEndsRequest) {
  auto local_reply = filter("local_reply");
  AiRequest request = chatRequest();
  // The module returns Error after replying, which the reply makes moot.
  EXPECT_OK(run(*local_reply, request));
  EXPECT_EQ(reply_code_, Http::Code::Forbidden);
  EXPECT_EQ(reply_body_, "blocked");
  // Out-of-range code rejected, first reply sent, second reply rejected.
  EXPECT_EQ(filterState("local_reply"), "0,1,0");
}

TEST_F(DynamicModuleAiFilterTest, RewritesPayloadAndHeaders) {
  headers_.addCopy("x-tenant", "acme");
  headers_.addCopy("x-remove-me", "1");
  auto rewrite = filter("rewrite");
  AiRequest request = chatRequest();
  EXPECT_OK(run(*rewrite, request));

  const nlohmann::json& payload = request.json();
  EXPECT_EQ(payload["model"], "rewritten");
  EXPECT_EQ(payload["metadata"], nlohmann::json::parse(R"({"tagged":true})"));
  EXPECT_FALSE(payload.contains("temperature"));
  ASSERT_EQ(payload["messages"].size(), 2);
  EXPECT_TRUE(JsonWithExtBuf::isExternalRef(payload["messages"][0]["content"]));
  EXPECT_EQ(payload["messages"][1]["content"], "appended");

  EXPECT_EQ(headers_.get_("x-protocol"), "OPENAI_CHAT_COMPLETIONS");
  EXPECT_EQ(headers_.get_("x-model"), "\"gpt-4o\"");
  EXPECT_EQ(headers_.get_("x-tenant-echo"), "acme");
  EXPECT_FALSE(headers_.has("x-remove-me"));
  EXPECT_EQ(filterState("tenant"), "acme");
  EXPECT_EQ(filterState("first_content"),
            std::to_string(envoy_dynamic_module_type_ai_filter_json_result_Offloaded));
}

TEST_F(DynamicModuleAiFilterTest, UnknownProtocolIsNotReported) {
  auto rewrite = filter("rewrite");
  AiRequest request = chatRequest(LLMProtocol::Unspecified);
  EXPECT_OK(run(*rewrite, request));
  EXPECT_FALSE(headers_.has("x-protocol"));
}

TEST_F(DynamicModuleAiFilterTest, PayloadIsUnavailableOutsideTheRequestHook) {
  ASSERT_NE(filter("outside_request"), nullptr);
  const std::string unavailable =
      std::to_string(envoy_dynamic_module_type_ai_filter_json_result_Unavailable);
  EXPECT_EQ(filterState("outside_request"),
            absl::StrCat(unavailable, ",", unavailable, ",", unavailable, ",0,0"));
}

TEST_F(DynamicModuleAiFilterTest, RejectsInvalidHeaders) {
  auto invalid_headers = filter("invalid_headers");
  AiRequest request = chatRequest();
  EXPECT_OK(run(*invalid_headers, request));
  // Empty, malformed, CR/LF and NUL writes and lookups fail; a valid write still succeeds.
  EXPECT_EQ(filterState("invalid_headers"), "0,0,0,0,0,0,1");
  EXPECT_EQ(headers_.get_("x-valid"), "ok");
  EXPECT_FALSE(headers_.has("x-crlf"));
  EXPECT_FALSE(headers_.has("x-nul"));
  EXPECT_FALSE(headers_.has("x-injected"));
}

TEST_F(DynamicModuleAiFilterTest, StreamCallbacksFailOutsideHooks) {
  headers_.addCopy("x-tenant", "acme");
  auto passthrough = filter("passthrough");
  ASSERT_NE(passthrough, nullptr);
  envoy_dynamic_module_type_envoy_buffer result{"stale", 5};
  size_t total_count = 1;
  EXPECT_FALSE(envoy_dynamic_module_callback_ai_filter_get_request_header_value(
      passthrough.get(), {"x-tenant", 8}, &result, 0, &total_count));
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(total_count, 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_ai_filter_set_request_header(
      passthrough.get(), {"x-tenant", 8}, {"other", 5}));
  EXPECT_FALSE(envoy_dynamic_module_callback_ai_filter_remove_request_header(passthrough.get(),
                                                                             {"x-tenant", 8}));
  EXPECT_FALSE(envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(
      passthrough.get(), {"tenant", 6}, {"acme", 4}));
  EXPECT_FALSE(envoy_dynamic_module_callback_ai_filter_get_filter_state_bytes(
      passthrough.get(), {"tenant", 6}, &result));
  EXPECT_EQ(headers_.get_("x-tenant"), "acme");
  EXPECT_EQ(filterState("tenant"), "");
}

} // namespace
} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
