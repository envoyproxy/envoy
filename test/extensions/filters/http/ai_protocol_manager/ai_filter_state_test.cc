#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/downstream_api.pb.h"
#include "envoy/type/ai/v3/upstream_target.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using envoy::type::ai::v3::DownstreamApi;
using envoy::type::ai::v3::UpstreamTarget;

const StreamInfo::FilterState::ObjectFactory& objectFactory(absl::string_view key) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(key);
  EXPECT_NE(factory, nullptr);
  return *factory;
}

std::unique_ptr<StreamInfo::FilterState::Object> downstreamApi(absl::string_view data) {
  return objectFactory(DownstreamApiState::kFilterStateKey).createFromBytes(data);
}

const DownstreamApiState*
asDownstreamApi(const std::unique_ptr<StreamInfo::FilterState::Object>& o) {
  return dynamic_cast<const DownstreamApiState*>(o.get());
}

std::unique_ptr<StreamInfo::FilterState::Object> upstreamTarget(absl::string_view json) {
  return objectFactory(UpstreamTargetState::kFilterStateKey).createFromBytes(json);
}

const UpstreamTargetState*
asUpstreamTarget(const std::unique_ptr<StreamInfo::FilterState::Object>& o) {
  return dynamic_cast<const UpstreamTargetState*>(o.get());
}

absl::string_view stringField(const StreamInfo::FilterState::Object& object,
                              absl::string_view name) {
  const StreamInfo::FilterState::Object::FieldType field = object.getField(name);
  EXPECT_TRUE(absl::holds_alternative<absl::string_view>(field)) << name;
  return absl::holds_alternative<absl::string_view>(field) ? absl::get<absl::string_view>(field)
                                                           : absl::string_view();
}

TEST(DownstreamApiStateTest, FactoryBuildsFromShorthand) {
  const auto object = downstreamApi("OPENAI_RESPONSES");
  const DownstreamApiState* state = asDownstreamApi(object);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->protocol(), LLMProtocol::OpenAiResponses);
  EXPECT_FALSE(state->api()->has_endpoint());
  EXPECT_EQ(state->serializeAsString(), R"({"llm_protocol":"OPENAI_RESPONSES"})");
}

TEST(DownstreamApiStateTest, FactoryBuildsFromJson) {
  const auto object = downstreamApi(
      R"({"llm_protocol":"GEMINI_GENERATE_CONTENT","endpoint":{"preset":"gemini_api"}})");
  const DownstreamApiState* state = asDownstreamApi(object);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->protocol(), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(state->api()->endpoint().preset(), "gemini_api");
}

TEST(DownstreamApiStateTest, FactoryRejectsInvalidValues) {
  // Neither a value name nor JSON.
  EXPECT_EQ(downstreamApi("openai_chat_completions"), nullptr);
  EXPECT_EQ(downstreamApi("NOT_AN_API"), nullptr);
  EXPECT_EQ(downstreamApi(""), nullptr);
  // No protocol, in either form.
  EXPECT_EQ(downstreamApi("LLM_PROTOCOL_UNSPECIFIED"), nullptr);
  EXPECT_EQ(downstreamApi("{}"), nullptr);
  // PGV: a value the enum does not define.
  EXPECT_EQ(downstreamApi(R"({"llm_protocol":99})"), nullptr);
  // An unknown field.
  EXPECT_EQ(downstreamApi(R"({"llm_protocol":"ANTHROPIC_MESSAGES","authority":"x"})"), nullptr);
  // PGV: an endpoint with no layout.
  EXPECT_EQ(downstreamApi(R"({"llm_protocol":"ANTHROPIC_MESSAGES","endpoint":{}})"), nullptr);
  // An endpoint that does not serve the protocol.
  EXPECT_EQ(
      downstreamApi(R"({"llm_protocol":"ANTHROPIC_MESSAGES","endpoint":{"preset":"openai"}})"),
      nullptr);
}

TEST(DownstreamApiStateTest, Fields) {
  const auto with_preset =
      downstreamApi(R"({"llm_protocol":"ANTHROPIC_MESSAGES","endpoint":{"preset":"anthropic"}})");
  ASSERT_NE(with_preset, nullptr);
  EXPECT_TRUE(with_preset->hasFieldSupport());
  EXPECT_EQ(stringField(*with_preset, "llm_protocol"), "ANTHROPIC_MESSAGES");
  EXPECT_EQ(stringField(*with_preset, "preset"), "anthropic");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(with_preset->getField("model")));

  const auto with_template = downstreamApi(
      R"({"llm_protocol":"OPENAI_CHAT_COMPLETIONS","endpoint":{"custom":{"path_template":"/chat"}}})");
  ASSERT_NE(with_template, nullptr);
  EXPECT_EQ(stringField(*with_template, "preset"), "");

  const auto bare = downstreamApi("OPENAI_CHAT_COMPLETIONS");
  ASSERT_NE(bare, nullptr);
  EXPECT_EQ(stringField(*bare, "preset"), "");
}

TEST(DownstreamApiStateTest, SerializesAsJsonAndProto) {
  const std::string json =
      R"({"llm_protocol":"ANTHROPIC_MESSAGES","endpoint":{"preset":"aws_bedrock","variables":{"region":"us-east-1"}}})";
  const auto object = downstreamApi(json);
  ASSERT_NE(object, nullptr);
  const std::optional<std::string> serialized = object->serializeAsString();
  ASSERT_TRUE(serialized.has_value());
  EXPECT_EQ(*serialized, json);
  ASSERT_NE(downstreamApi(*serialized), nullptr);

  const ProtobufTypes::MessagePtr proto = object->serializeAsProto();
  ASSERT_NE(proto, nullptr);
  EXPECT_TRUE(TestUtility::protoEqual(*proto, *asDownstreamApi(object)->api()));
}

TEST(DownstreamApiStateTest, ReadsBackFromFilterState) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(DownstreamApiState::fromFilterState(filter_state), nullptr);

  auto api = std::make_shared<DownstreamApi>();
  api->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  filter_state.setData(DownstreamApiState::kFilterStateKey,
                       std::make_shared<DownstreamApiState>(api),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  const DownstreamApiState* state = DownstreamApiState::fromFilterState(filter_state);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->api(), api);
  EXPECT_EQ(state->protocol(), LLMProtocol::AnthropicMessages);
}

TEST(DownstreamApiStateTest, ForeignObjectIsNotAnApi) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state.setData(DownstreamApiState::kFilterStateKey,
                       std::make_shared<Router::StringAccessorImpl>("ANTHROPIC_MESSAGES"),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(DownstreamApiState::fromFilterState(filter_state), nullptr);
}

TEST(UpstreamTargetStateTest, FactoryBuildsFromJson) {
  const auto object = upstreamTarget(R"({"llm_protocol":"ANTHROPIC_MESSAGES"})");
  const UpstreamTargetState* state = asUpstreamTarget(object);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->protocol(), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(state->serializeAsString(), R"({"llm_protocol":"ANTHROPIC_MESSAGES"})");
}

TEST(UpstreamTargetStateTest, FieldsAndSerialization) {
  const std::string json =
      R"({"llm_protocol":"ANTHROPIC_MESSAGES","authority":"bedrock-runtime.us-east-1.amazonaws.com",)"
      R"("endpoint":{"preset":"aws_bedrock","variables":{"region":"us-east-1"}},)"
      R"("model":"anthropic.claude-sonnet-4-5","credential":"bedrock-prod"})";
  const auto object = upstreamTarget(json);
  const UpstreamTargetState* state = asUpstreamTarget(object);
  ASSERT_NE(state, nullptr);

  EXPECT_TRUE(state->hasFieldSupport());
  EXPECT_EQ(stringField(*state, "llm_protocol"), "ANTHROPIC_MESSAGES");
  EXPECT_EQ(stringField(*state, "authority"), "bedrock-runtime.us-east-1.amazonaws.com");
  EXPECT_EQ(stringField(*state, "model"), "anthropic.claude-sonnet-4-5");
  EXPECT_EQ(stringField(*state, "preset"), "aws_bedrock");
  EXPECT_EQ(stringField(*state, "credential"), "bedrock-prod");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(state->getField("region")));

  const std::optional<std::string> serialized = state->serializeAsString();
  ASSERT_TRUE(serialized.has_value());
  EXPECT_EQ(*serialized, json);
  ASSERT_NE(upstreamTarget(*serialized), nullptr);

  const ProtobufTypes::MessagePtr proto = state->serializeAsProto();
  ASSERT_NE(proto, nullptr);
  EXPECT_TRUE(TestUtility::protoEqual(*proto, *state->target()));
}

TEST(UpstreamTargetStateTest, UnsetFieldsAreEmpty) {
  const auto object = upstreamTarget(R"({"llm_protocol":"OPENAI_CHAT_COMPLETIONS"})");
  ASSERT_NE(object, nullptr);
  for (const absl::string_view field : {"authority", "model", "preset", "credential"}) {
    EXPECT_EQ(stringField(*object, field), "") << field;
  }
}

TEST(UpstreamTargetStateTest, FactoryRejectsInvalidTargets) {
  // Not JSON.
  EXPECT_EQ(upstreamTarget("{"), nullptr);
  EXPECT_EQ(upstreamTarget("ANTHROPIC_MESSAGES"), nullptr);
  // An unknown field.
  EXPECT_EQ(upstreamTarget(R"({"llm_protocol":"ANTHROPIC_MESSAGES","region":"x"})"), nullptr);
  // PGV: a value the enum does not define.
  EXPECT_EQ(upstreamTarget(R"({"llm_protocol":99})"), nullptr);
  // No protocol.
  EXPECT_EQ(upstreamTarget("{}"), nullptr);
  EXPECT_EQ(upstreamTarget(R"({"llm_protocol":"LLM_PROTOCOL_UNSPECIFIED"})"), nullptr);
  // An endpoint that does not serve the protocol, or is malformed.
  EXPECT_EQ(
      upstreamTarget(R"({"llm_protocol":"OPENAI_RESPONSES","endpoint":{"preset":"azure_openai",)"
                     R"("variables":{"api_version":"2024-10-21"}}})"),
      nullptr);
  EXPECT_EQ(upstreamTarget(R"({"llm_protocol":"OPENAI_RESPONSES","endpoint":{"custom":{}}})"),
            nullptr);
  // PGV: an empty preset, or a placement or framing the enum does not define.
  EXPECT_EQ(
      upstreamTarget(R"({"llm_protocol":"OPENAI_CHAT_COMPLETIONS","endpoint":{"preset":""}})"),
      nullptr);
  for (const absl::string_view field :
       {"model_placement", "stream_placement", "response_framing"}) {
    EXPECT_EQ(upstreamTarget(absl::StrCat(R"({"llm_protocol":"OPENAI_CHAT_COMPLETIONS",)",
                                          R"("endpoint":{"custom":{"path_template":"/m",")", field,
                                          R"(":7}}})")),
              nullptr)
        << field;
  }
}

std::string targetWith(absl::string_view field, absl::string_view json_value) {
  return absl::StrCat(R"({"llm_protocol":"OPENAI_CHAT_COMPLETIONS",")", field, R"(":)", json_value,
                      "}");
}

TEST(UpstreamTargetStateTest, Authority) {
  for (const absl::string_view valid :
       {"api.openai.com", "api.openai.com:443", "10.0.0.1:8080", "[::1]", "[::1]:443", "a:65535"}) {
    EXPECT_NE(upstreamTarget(targetWith("authority", absl::StrCat("\"", valid, "\""))), nullptr)
        << valid;
  }
  for (const absl::string_view invalid :
       {"a/b", "user@host", "host:", "host:0", "host:65536", "host:123456", "host:abc", "host:+1",
        ":443", "a b", "a\\r\\nb", "a#b"}) {
    EXPECT_EQ(upstreamTarget(targetWith("authority", absl::StrCat("\"", invalid, "\""))), nullptr)
        << invalid;
  }
}

TEST(UpstreamTargetStateTest, Model) {
  EXPECT_NE(upstreamTarget(targetWith("model", R"("claude-sonnet-4-5@20250929")")), nullptr);
  EXPECT_NE(upstreamTarget(targetWith("model", absl::StrCat("\"", std::string(256, 'm'), "\""))),
            nullptr);
  EXPECT_EQ(upstreamTarget(targetWith("model", absl::StrCat("\"", std::string(257, 'm'), "\""))),
            nullptr);
  for (const absl::string_view invalid : {R"("a\rb")", R"("a\nb")", R"("a\u0000b")"}) {
    EXPECT_EQ(upstreamTarget(targetWith("model", invalid)), nullptr) << invalid;
  }
}

TEST(UpstreamTargetStateTest, Credential) {
  EXPECT_NE(upstreamTarget(targetWith("credential", R"("openai_prod-1.key")")), nullptr);
  EXPECT_NE(
      upstreamTarget(targetWith("credential", absl::StrCat("\"", std::string(128, 'c'), "\""))),
      nullptr);
  EXPECT_EQ(
      upstreamTarget(targetWith("credential", absl::StrCat("\"", std::string(129, 'c'), "\""))),
      nullptr);
  for (const absl::string_view invalid : {R"("sk live")", R"("a/b")", R"("a:b")", R"("a@b")"}) {
    EXPECT_EQ(upstreamTarget(targetWith("credential", invalid)), nullptr) << invalid;
  }
}

TEST(UpstreamTargetStateTest, ReadsBackFromFilterState) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(UpstreamTargetState::fromFilterState(filter_state), nullptr);

  auto target = std::make_shared<UpstreamTarget>();
  target->set_llm_protocol(envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  filter_state.setData(UpstreamTargetState::kFilterStateKey,
                       std::make_shared<UpstreamTargetState>(target),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  const UpstreamTargetState* state = UpstreamTargetState::fromFilterState(filter_state);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->target(), target);
  EXPECT_EQ(state->protocol(), LLMProtocol::GeminiGenerateContent);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
