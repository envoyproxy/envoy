#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_state.h"

#include "absl/types/variant.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

const StreamInfo::FilterState::ObjectFactory& objectFactory(absl::string_view key) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(key);
  EXPECT_NE(factory, nullptr);
  return *factory;
}

TEST(RequestLlmProtocolTest, SerializesAsEnumValueName) {
  EXPECT_EQ(RequestLlmProtocol(LLMProtocol::GeminiGenerateContent).serializeAsString(),
            "GEMINI_GENERATE_CONTENT");
  EXPECT_EQ(RequestLlmProtocol(LLMProtocol::Unspecified).serializeAsString(),
            "LLM_PROTOCOL_UNSPECIFIED");
}

TEST(RequestLlmProtocolTest, ExposesLlmProtocolField) {
  const RequestLlmProtocol object(LLMProtocol::OpenAiChatCompletions);
  EXPECT_TRUE(object.hasFieldSupport());
  EXPECT_EQ(absl::get<absl::string_view>(object.getField("llm_protocol")),
            "OPENAI_CHAT_COMPLETIONS");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(object.getField("model")));
}

TEST(RequestLlmProtocolTest, FactoryBuildsFromEnumValueName) {
  const auto object =
      objectFactory(RequestLlmProtocol::kFilterStateKey).createFromBytes("OPENAI_RESPONSES");
  const auto* typed = dynamic_cast<const RequestLlmProtocol*>(object.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->protocol(), LLMProtocol::OpenAiResponses);
}

TEST(RequestLlmProtocolTest, FactoryBuildsUnspecified) {
  const auto object = objectFactory(RequestLlmProtocol::kFilterStateKey)
                          .createFromBytes("LLM_PROTOCOL_UNSPECIFIED");
  const auto* typed = dynamic_cast<const RequestLlmProtocol*>(object.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->protocol(), LLMProtocol::Unspecified);
}

// A name the enum does not define yields nothing, so a typo cannot read as an unspecified protocol.
TEST(RequestLlmProtocolTest, FactoryRejectsUnknownName) {
  const auto& factory = objectFactory(RequestLlmProtocol::kFilterStateKey);
  EXPECT_EQ(factory.createFromBytes("openai_chat_completions"), nullptr);
  EXPECT_EQ(factory.createFromBytes("NOT_AN_API"), nullptr);
  EXPECT_EQ(factory.createFromBytes(""), nullptr);
}

TEST(RequestLlmProtocolTest, ReadsBackFromFilterState) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(RequestLlmProtocol::fromFilterState(filter_state), LLMProtocol::Unspecified);
  filter_state.setData(RequestLlmProtocol::kFilterStateKey,
                       std::make_shared<RequestLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(RequestLlmProtocol::fromFilterState(filter_state), LLMProtocol::AnthropicMessages);
}

std::unique_ptr<StreamInfo::FilterState::Object> upstreamTargetFromJson(absl::string_view json) {
  return objectFactory(UpstreamTargetState::kFilterStateKey).createFromBytes(json);
}

TEST(UpstreamTargetStateTest, FactoryBuildsFromJson) {
  const auto object = upstreamTargetFromJson(R"({"llm_protocol":"ANTHROPIC_MESSAGES"})");
  const auto* typed = dynamic_cast<const UpstreamTargetState*>(object.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->protocol(), LLMProtocol::AnthropicMessages);

  EXPECT_TRUE(typed->hasFieldSupport());
  EXPECT_EQ(absl::get<absl::string_view>(typed->getField("llm_protocol")), "ANTHROPIC_MESSAGES");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(typed->getField("model")));

  // The serialized form builds the same target back.
  const std::optional<std::string> json = typed->serializeAsString();
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(*json, R"({"llm_protocol":"ANTHROPIC_MESSAGES"})");
  const auto round_trip = upstreamTargetFromJson(*json);
  const auto* typed_round_trip = dynamic_cast<const UpstreamTargetState*>(round_trip.get());
  ASSERT_NE(typed_round_trip, nullptr);
  EXPECT_EQ(typed_round_trip->protocol(), LLMProtocol::AnthropicMessages);
}

TEST(UpstreamTargetStateTest, FactoryRejectsInvalidTargets) {
  // Not JSON.
  EXPECT_EQ(upstreamTargetFromJson("{"), nullptr);
  // An unknown field.
  EXPECT_EQ(upstreamTargetFromJson(R"({"llm_protocol":"ANTHROPIC_MESSAGES","region":"x"})"),
            nullptr);
  // PGV: a value the enum does not define.
  EXPECT_EQ(upstreamTargetFromJson(R"({"llm_protocol":99})"), nullptr);
  // No protocol.
  EXPECT_EQ(upstreamTargetFromJson("{}"), nullptr);
  EXPECT_EQ(upstreamTargetFromJson(R"({"llm_protocol":"LLM_PROTOCOL_UNSPECIFIED"})"), nullptr);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
