#include <optional>
#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/request_info/extractor.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {
namespace {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::llmProtocolName;
using HttpFilters::AiProtocolManager::MaxStringValueSize;

nlohmann::json parse(const std::string& json) {
  nlohmann::json result = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(result.is_discarded()) << json;
  return result;
}

RequestAttributes extract(LLMProtocol protocol, const std::string& json,
                          absl::string_view path = "/v1/chat/completions") {
  return extractRequestAttributes(protocol, parse(json), path);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletions) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiChatCompletions,
                                          R"({"model":"gpt-4o","stream":true,"max_tokens":128,
          "messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}],
          "tools":[{"type":"function","function":{"name":"f"}}]})");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(attrs.model, "gpt-4o");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 128u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletionsPrefersMaxCompletionTokens) {
  EXPECT_EQ(extract(LLMProtocol::OpenAiChatCompletions,
                    R"({"max_completion_tokens":64,"max_tokens":128})")
                .max_output_tokens,
            64u);
  EXPECT_EQ(extract(LLMProtocol::OpenAiChatCompletions, R"({"max_tokens":128})").max_output_tokens,
            128u);
}

TEST(RequestInfoExtractorTest, OpenAiResponses) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiResponses,
                                          R"({"model":"gpt-5","stream":false,"max_output_tokens":50,
          "input":[{"role":"user","content":"a"},{"role":"user","content":"b"},
                   {"role":"user","content":"c"}],
          "tools":[{"type":"web_search"},{"type":"function","name":"f"}]})",
                                          "/v1/responses");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::OpenAiResponses);
  EXPECT_EQ(attrs.model, "gpt-5");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 50u);
  EXPECT_EQ(attrs.message_count, 3u);
  EXPECT_EQ(attrs.tool_count, 2u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesStringInputIsOneMessage) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiResponses, R"({"model":"gpt-5","input":"hello"})");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesOffloadedStringInputIsOneMessage) {
  nlohmann::json json = parse(R"({"model":"gpt-5"})");
  json["input"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 2000});
  const RequestAttributes attrs =
      extractRequestAttributes(LLMProtocol::OpenAiResponses, json, "/v1/responses");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AnthropicMessages) {
  const RequestAttributes attrs =
      extract(LLMProtocol::AnthropicMessages,
              R"({"model":"claude-opus-5","max_tokens":1024,"stream":true,"system":"be nice",
          "messages":[{"role":"user","content":"hi"}],
          "tools":[{"name":"t","input_schema":{"type":"object"}}]})",
              "/v1/messages");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::AnthropicMessages);
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 1024u);
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiReadsTargetAndGenerationConfig) {
  const RequestAttributes attrs =
      extract(LLMProtocol::GeminiGenerateContent,
              R"({"contents":[{"role":"user","parts":[{"text":"hi"}]},
                      {"role":"model","parts":[{"text":"yo"}]}],
          "generationConfig":{"maxOutputTokens":64},
          "tools":[{"functionDeclarations":[]}]})",
              "/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(attrs.model, "gemini-2.5-pro");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 64u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiAcceptsSnakeCaseAndVertexTarget) {
  const RequestAttributes attrs =
      extract(LLMProtocol::GeminiGenerateContent,
              R"({"contents":[],"generation_config":{"max_output_tokens":32}})",
              "/v1/projects/p/locations/us-central1/publishers/google/models/"
              "gemini-2.5-flash:generateContent");
  EXPECT_EQ(attrs.model, "gemini-2.5-flash");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 32u);
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiIgnoresUnrecognizedTarget) {
  for (const absl::string_view path :
       {"/v1beta/models/gemini-2.5-pro:countTokens", "/v1beta/gemini-2.5-pro:generateContent",
        "/v1beta/models/gemini-2.5-pro", "/v1beta/models/:generateContent", "/"}) {
    const RequestAttributes attrs = extract(LLMProtocol::GeminiGenerateContent, "{}", path);
    EXPECT_TRUE(attrs.model.empty()) << path;
    EXPECT_FALSE(attrs.stream.has_value()) << path;
    EXPECT_FALSE(attrs.malformed) << path;
  }
}

TEST(RequestInfoExtractorTest, GeminiOversizedPathModelFlagsAndKeepsStream) {
  const RequestAttributes attrs = extract(
      LLMProtocol::GeminiGenerateContent, "{}",
      "/v1beta/models/" + std::string(MaxStringValueSize + 1, 'm') + ":streamGenerateContent");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.stream, true);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, UnspecifiedReadsSharedAttributesOnly) {
  const RequestAttributes attrs = extract(LLMProtocol::Unspecified,
                                          R"({"model":"m","stream":false,"max_tokens":5,
          "messages":[{"role":"user","content":"hi"}],"tools":[]})");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::Unspecified);
  EXPECT_EQ(attrs.model, "m");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AbsentAttributesStayAbsent) {
  for (const LLMProtocol protocol :
       {LLMProtocol::Unspecified, LLMProtocol::OpenAiChatCompletions, LLMProtocol::OpenAiResponses,
        LLMProtocol::AnthropicMessages, LLMProtocol::GeminiGenerateContent}) {
    const RequestAttributes attrs = extract(protocol, "{}", "/");
    EXPECT_EQ(attrs.llm_protocol, protocol);
    EXPECT_TRUE(attrs.model.empty()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.stream.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.max_output_tokens.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.message_count.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.tool_count.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.malformed) << llmProtocolName(protocol);
  }
}

TEST(RequestInfoExtractorTest, NullAttributesReadAsAbsent) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
              R"({"model":"gpt-4o","stream":null,"max_tokens":null,"max_completion_tokens":null,
          "tools":null,"messages":[]})");
  EXPECT_EQ(attrs.model, "gpt-4o");
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, UnusableAttributesReadAsAbsentAndFlag) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
              R"({"model":42,"stream":"yes","max_tokens":-1,"messages":"nope","tools":{}})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OneUnusableAttributeKeepsTheRest) {
  const RequestAttributes attrs =
      extract(LLMProtocol::AnthropicMessages,
              R"({"model":"claude-opus-5","max_tokens":"lots","messages":[{"role":"user"}]})",
              "/v1/messages");
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OffloadedModelReadsAsAbsentAndFlags) {
  nlohmann::json json = parse(R"({"messages":[]})");
  json["model"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 20});
  const RequestAttributes attrs =
      extractRequestAttributes(LLMProtocol::AnthropicMessages, json, "/v1/messages");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OversizedModelReadsAsAbsentAndFlags) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
              R"({"model":")" + std::string(MaxStringValueSize + 1, 'm') + R"("})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, EmptyModelReadsAsAbsentWithoutFlag) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiChatCompletions, R"({"model":""})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.malformed);
}

// Configured extraction.

ExtractionRule rule(std::vector<std::string> path, std::string key = "",
                    std::optional<Protobuf::Value> on_missing = std::nullopt) {
  ExtractionRule r;
  r.path = std::move(path);
  r.key = key.empty() ? absl::StrJoin(r.path, ".") : std::move(key);
  r.on_missing = std::move(on_missing);
  return r;
}

Protobuf::Value stringValue(absl::string_view s) {
  Protobuf::Value v;
  v.set_string_value(std::string(s));
  return v;
}

nlohmann::json asJson(const Protobuf::Struct& s) {
  return parse(MessageUtil::getJsonStringFromMessageOrError(s));
}

struct Extracted {
  ExtractionOutcome outcome;
  Protobuf::Struct out;
  nlohmann::json json() const { return asJson(out); }
};

Extracted extractConfigured(const nlohmann::json& json, const std::vector<ExtractionRule>& rules,
                            const ExtractionLimits& limits = ExtractionLimits{},
                            const Protobuf::Struct* existing = nullptr) {
  Extracted result;
  result.outcome = extractConfiguredValues(json, rules, limits, existing, result.out);
  return result;
}

TEST(RequestInfoConfiguredExtractionTest, CopiesStructuredPolicyValuesWhole) {
  const Extracted e =
      extractConfigured(parse(R"({"model":"auto","contents":[{"parts":[{"text":"hi"}]}],
                "settings":[{"cost":"low","quality":"high","weight":0.5,"pinned":true,"n":null}],
                "model_list":["model_a","model_b"]})"),
                        {rule({"settings"}), rule({"model_list"})});
  EXPECT_EQ(e.outcome.written, 2u);
  EXPECT_EQ(e.outcome.dropped, 0u);
  EXPECT_EQ(e.outcome.unusable, 0u);
  EXPECT_EQ(e.json(), parse(R"({"settings":[{"cost":"low","quality":"high","weight":0.5,
                                              "pinned":true,"n":null}],
                                 "model_list":["model_a","model_b"]})"));
}

TEST(RequestInfoConfiguredExtractionTest, NestedPathAndDefaultKey) {
  const Extracted e =
      extractConfigured(parse(R"({"generationConfig":{"thinkingConfig":{"thinkingBudget":1024}}})"),
                        {rule({"generationConfig", "thinkingConfig", "thinkingBudget"}),
                         rule({"generationConfig", "thinkingConfig"}, "thinking")});
  EXPECT_EQ(e.json(), parse(R"({"generationConfig.thinkingConfig.thinkingBudget":1024,
                                 "thinking":{"thinkingBudget":1024}})"));
}

TEST(RequestInfoConfiguredExtractionTest, AbsentNullAndNonObjectStepsReadAsMissing) {
  const Extracted e = extractConfigured(
      parse(R"({"a":null,"b":"scalar","c":{"d":1}})"),
      {rule({"a"}), rule({"b", "x"}), rule({"c", "e"}), rule({"z"}), rule({"c", "d", "deeper"})});
  EXPECT_EQ(e.outcome.written, 0u);
  EXPECT_EQ(e.outcome.dropped, 0u);
  EXPECT_EQ(e.outcome.unusable, 0u);
  EXPECT_TRUE(e.out.fields().empty());
}

TEST(RequestInfoConfiguredExtractionTest, OnMissingAppliesOnlyWhenNoRuleYieldsTheKey) {
  // A real value from a later rule beats an earlier rule's default.
  Extracted e = extractConfigured(
      parse(R"({"model_list":["m"]})"),
      {rule({"modelList"}, "models", stringValue("default")), rule({"model_list"}, "models")});
  EXPECT_EQ(e.json(), parse(R"({"models":["m"]})"));
  EXPECT_EQ(e.outcome.written, 1u);

  // No value anywhere: the first default wins.
  e = extractConfigured(parse("{}"), {rule({"modelList"}, "models", stringValue("first")),
                                      rule({"model_list"}, "models", stringValue("second"))});
  EXPECT_EQ(e.json(), parse(R"({"models":"first"})"));
  EXPECT_EQ(e.outcome.written, 1u);
}

TEST(RequestInfoConfiguredExtractionTest, FirstRuleToYieldAKeyWins) {
  const Extracted e =
      extractConfigured(parse(R"({"a":1,"b":2})"), {rule({"a"}, "k"), rule({"b"}, "k")});
  EXPECT_EQ(e.json(), parse(R"({"k":1})"));
  EXPECT_EQ(e.outcome.written, 1u);
}

TEST(RequestInfoConfiguredExtractionTest, ExistingKeysAreLeftAlone) {
  Protobuf::Struct existing;
  (*existing.mutable_fields())["settings"] = stringValue("earlier");
  const Extracted e = extractConfigured(
      parse(R"({"settings":[1],"model_list":[2]})"),
      {rule({"settings"}), rule({"model_list"}), rule({"missing"}, "settings", stringValue("d"))},
      ExtractionLimits{}, &existing);
  EXPECT_EQ(e.json(), parse(R"({"model_list":[2]})"));
  EXPECT_EQ(e.outcome.written, 1u);
}

TEST(RequestInfoConfiguredExtractionTest, OversizedValueIsDroppedWholeAndCountsOnce) {
  ExtractionLimits limits;
  limits.max_value_bytes = 64;
  const Extracted e = extractConfigured(
      parse(R"({"big":["aaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbb","cccccccccccccccc","dddddddddddddddd"],
                "small":"ok"})"),
      {rule({"big"}), rule({"small"})}, limits);
  EXPECT_EQ(e.outcome.dropped, 1u);
  EXPECT_EQ(e.outcome.written, 1u);
  EXPECT_EQ(e.json(), parse(R"({"small":"ok"})"));
}

TEST(RequestInfoConfiguredExtractionTest, TotalBudgetSpansRulesAndDroppedValuesRefund) {
  ExtractionLimits limits;
  limits.max_value_bytes = 1024;
  limits.max_total_bytes = 64;
  // 8 overhead + 20 bytes = 28 each: two fit in 64, the third does not.
  const nlohmann::json json = parse(R"({"a":"aaaaaaaaaaaaaaaaaaaa","b":"bbbbbbbbbbbbbbbbbbbb",
                                        "c":"cccccccccccccccccccc","huge":"x"})");
  Extracted e = extractConfigured(json, {rule({"a"}), rule({"b"}), rule({"c"})}, limits);
  EXPECT_EQ(e.outcome.written, 2u);
  EXPECT_EQ(e.outcome.dropped, 1u);
  EXPECT_EQ(e.json(), parse(R"({"a":"aaaaaaaaaaaaaaaaaaaa","b":"bbbbbbbbbbbbbbbbbbbb"})"));

  // A dropped value spends nothing, so it cannot starve the rules after it.
  limits.max_total_bytes = 40;
  e = extractConfigured(json, {rule({"a"}), rule({"b"}), rule({"huge"})}, limits);
  EXPECT_EQ(e.outcome.written, 2u);
  EXPECT_EQ(e.outcome.dropped, 1u);
  EXPECT_EQ(e.json(), parse(R"({"a":"aaaaaaaaaaaaaaaaaaaa","huge":"x"})"));
}

TEST(RequestInfoConfiguredExtractionTest, DepthLimitCountsBelowThePath) {
  ExtractionLimits limits;
  limits.max_depth = 2;
  const nlohmann::json json = parse(R"({"settings":[{"cost":"low"}],"deep":[[["x"]]]})");
  const Extracted e = extractConfigured(json, {rule({"settings"}), rule({"deep"})}, limits);
  EXPECT_EQ(e.json(), parse(R"({"settings":[{"cost":"low"}]})"));
  EXPECT_EQ(e.outcome.dropped, 1u);

  // A scalar at the path is depth 0 and always fits.
  limits.max_depth = 1;
  EXPECT_EQ(extractConfigured(parse(R"({"s":"v","l":["v"],"o":{"k":["v"]}})"),
                              {rule({"s"}), rule({"l"}), rule({"o"})}, limits)
                .json(),
            parse(R"({"s":"v","l":["v"]})"));
}

TEST(RequestInfoConfiguredExtractionTest, StringLimitAppliesToValuesAndKeys) {
  ExtractionLimits limits;
  limits.max_string_bytes = 4;
  const Extracted e =
      extractConfigured(parse(R"({"v":"12345","k":{"12345":1},"ok":{"abcd":"wxyz"}})"),
                        {rule({"v"}), rule({"k"}), rule({"ok"})}, limits);
  EXPECT_EQ(e.outcome.dropped, 2u);
  EXPECT_EQ(e.json(), parse(R"({"ok":{"abcd":"wxyz"}})"));
}

TEST(RequestInfoConfiguredExtractionTest, UnrepresentableNumbersAreUnusable) {
  const Extracted e = extractConfigured(
      parse(R"({"big":9007199254740993,"neg":-9007199254740993,"u":18446744073709551615,
                "fine":9007199254740991,"f":1.5,"nested":{"x":[1,18446744073709551615]}})"),
      {rule({"big"}), rule({"neg"}), rule({"u"}), rule({"fine"}), rule({"f"}), rule({"nested"})});
  EXPECT_EQ(e.outcome.unusable, 4u);
  EXPECT_EQ(e.outcome.written, 2u);
  EXPECT_EQ(e.json(), parse(R"({"fine":9007199254740991,"f":1.5})"));
}

TEST(RequestInfoConfiguredExtractionTest, OffloadedStringIsUnusableWhereverItSits) {
  nlohmann::json json = parse(R"({"settings":[{"cost":"low"}],"plain":"p"})");
  json["offloaded"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 2000});
  json["settings"][0]["note"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{3, 9});
  const Extracted e =
      extractConfigured(json, {rule({"offloaded"}), rule({"settings"}), rule({"plain"})});
  EXPECT_EQ(e.outcome.unusable, 2u);
  EXPECT_EQ(e.json(), parse(R"({"plain":"p"})"));
}

TEST(RequestInfoConfiguredExtractionTest, DefaultLimitsFitARealisticPolicyBlock) {
  nlohmann::json settings = nlohmann::json::array();
  for (int i = 0; i < 16; ++i) {
    settings.push_back({{"cost", "low"}, {"quality", "high"}, {"region", "us-central1"}});
  }
  nlohmann::json models = nlohmann::json::array();
  for (int i = 0; i < 32; ++i) {
    models.push_back("publishers/google/models/gemini-2.5-flash-" + std::to_string(i));
  }
  const nlohmann::json json{{"settings", settings}, {"model_list", models}};
  const Extracted e = extractConfigured(json, {rule({"settings"}), rule({"model_list"})});
  EXPECT_EQ(e.outcome.written, 2u);
  EXPECT_EQ(e.outcome.dropped, 0u);
  EXPECT_EQ(e.json(), json);
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
