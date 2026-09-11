#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/extensions/filters/ai/request_info/v3/request_info.pb.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/ai/request_info/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using HttpFilters::AiProtocolManager::ApiProtocol;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;

constexpr absl::string_view DefaultNamespace = "envoy.ai.request_info";

class RequestInfoFilterTest : public testing::Test {
public:
  RequestInfoFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_raw_(new FakeBridge(*dispatcher_)),
        buffer_manager_(factory_, std::unique_ptr<FakeBridge>(bridge_raw_)),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~RequestInfoFilterTest() override { buffer_manager_.onDestroy(); }

  RequestInfoFilterConfigSharedPtr makeConfig(const std::string& metadata_namespace = "") {
    envoy::extensions::filters::ai::request_info::v3::RequestInfo proto;
    proto.set_metadata_namespace(metadata_namespace);
    return std::make_shared<const RequestInfoFilterConfig>(proto, *stats_store_.rootScope());
  }

  // Returns the replayed body.
  std::string run(const std::string& payload, ApiProtocol protocol,
                  absl::string_view path = "/v1/chat/completions",
                  RequestInfoFilterConfigSharedPtr config = nullptr) {
    request_headers_ =
        Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", std::string(path)}};
    JsonWithExtBuf doc;
    doc.setJson(nlohmann::json::parse(payload));
    std::vector<AiFilterSharedPtr> filters;
    filters.push_back(std::make_unique<RequestInfoFilter>(
        config != nullptr ? std::move(config) : makeConfig(),
        AiFilterContext{stream_info_, request_headers_, protocol}));
    FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                          stream_info_);

    absl::Status status;
    bool completed = false;
    manager.start([&status, &completed](absl::Status s) {
      status = std::move(s);
      completed = true;
    });
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_TRUE(completed);
    EXPECT_TRUE(status.ok()) << status;
    return bridge_raw_->injected_.toString();
  }

  std::optional<envoy::data::ai::v3::RequestInfo>
  published(absl::string_view metadata_namespace = DefaultNamespace) {
    const auto& typed = stream_info_.dynamicMetadata().typed_filter_metadata();
    const auto it = typed.find(std::string(metadata_namespace));
    if (it == typed.end()) {
      return std::nullopt;
    }
    envoy::data::ai::v3::RequestInfo record;
    EXPECT_TRUE(it->second.UnpackTo(&record));
    return record;
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter =
        TestUtility::findCounter(stats_store_, "ai_protocol_manager.request_info." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge* bridge_raw_{nullptr};
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

TEST_F(RequestInfoFilterTest, PublishesTypedRecordAndForwardsPayloadUnchanged) {
  const std::string payload =
      R"({"model":"gpt-4o","stream":true,"max_completion_tokens":64,
          "messages":[{"role":"user","content":"hi"}],
          "tools":[{"type":"function","function":{"name":"f"}}]})";
  const std::string replayed = run(payload, ApiProtocol::OpenAiChatCompletions);
  EXPECT_EQ(nlohmann::json::parse(replayed), nlohmann::json::parse(payload));

  const auto record = published();
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->api_protocol(), envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  EXPECT_EQ(record->model(), "gpt-4o");
  EXPECT_TRUE(record->stream().value());
  EXPECT_EQ(record->max_output_tokens().value(), 64);
  EXPECT_EQ(record->message_count().value(), 1);
  EXPECT_EQ(record->tool_count().value(), 1);
  // Only typed metadata is published: no untyped mirror.
  EXPECT_TRUE(stream_info_.dynamicMetadata().filter_metadata().empty());
  EXPECT_EQ(counterValue("published"), 1);
  EXPECT_EQ(counterValue("partial"), 0);
  EXPECT_EQ(counterValue("duplicate"), 0);
}

TEST_F(RequestInfoFilterTest, AbsentAttributesAreLeftUnset) {
  run(R"({"messages":[]})", ApiProtocol::AnthropicMessages, "/v1/messages");
  const auto record = published();
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  EXPECT_TRUE(record->model().empty());
  EXPECT_FALSE(record->has_stream());
  EXPECT_FALSE(record->has_max_output_tokens());
  EXPECT_EQ(record->message_count().value(), 0);
  EXPECT_FALSE(record->has_tool_count());
}

TEST_F(RequestInfoFilterTest, FlagsPartialWhenAnAttributeIsUnusable) {
  run(R"({"model":42,"stream":true,"messages":[]})", ApiProtocol::OpenAiChatCompletions);
  const auto record = published();
  ASSERT_TRUE(record.has_value());
  EXPECT_TRUE(record->model().empty());
  EXPECT_TRUE(record->stream().value());
  EXPECT_EQ(counterValue("published"), 1);
  EXPECT_EQ(counterValue("partial"), 1);
}

TEST_F(RequestInfoFilterTest, ReadsGeminiTargetFromRequestPath) {
  run(R"({"contents":[{"parts":[{"text":"hi"}]}],"generationConfig":{"maxOutputTokens":32}})",
      ApiProtocol::GeminiGenerateContent,
      "/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse");
  const auto record = published();
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->api_protocol(), envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  EXPECT_EQ(record->model(), "gemini-2.5-pro");
  EXPECT_TRUE(record->stream().value());
  EXPECT_EQ(record->max_output_tokens().value(), 32);
  EXPECT_EQ(record->message_count().value(), 1);
}

TEST_F(RequestInfoFilterTest, UnspecifiedProtocolPublishesSharedAttributesOnly) {
  run(R"({"model":"m","stream":false,"max_tokens":5,"messages":[{"role":"user","content":"hi"}]})",
      ApiProtocol::Unspecified);
  const auto record = published();
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->api_protocol(), envoy::type::ai::v3::API_PROTOCOL_UNSPECIFIED);
  EXPECT_EQ(record->model(), "m");
  EXPECT_FALSE(record->stream().value());
  EXPECT_FALSE(record->has_max_output_tokens());
  EXPECT_FALSE(record->has_message_count());
}

TEST_F(RequestInfoFilterTest, PublishesUnderConfiguredNamespace) {
  run(R"({"model":"m"})", ApiProtocol::OpenAiChatCompletions, "/v1/chat/completions",
      makeConfig("custom.ns"));
  EXPECT_FALSE(published().has_value());
  ASSERT_TRUE(published("custom.ns").has_value());
  EXPECT_EQ(published("custom.ns")->model(), "m");
}

TEST_F(RequestInfoFilterTest, FirstWriterOwnsTheNamespace) {
  envoy::data::ai::v3::RequestInfo existing;
  existing.set_model("first");
  Protobuf::Any existing_any;
  ASSERT_TRUE(existing_any.PackFrom(existing));
  stream_info_.setDynamicTypedMetadata(std::string(DefaultNamespace), existing_any);

  run(R"({"model":"second"})", ApiProtocol::OpenAiChatCompletions);
  EXPECT_EQ(published()->model(), "first");
  EXPECT_EQ(counterValue("published"), 0);
  EXPECT_EQ(counterValue("duplicate"), 1);
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
