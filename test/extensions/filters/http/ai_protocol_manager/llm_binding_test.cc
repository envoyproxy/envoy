#include <optional>
#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include "test/test_common/status_utility.h"

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using ::Envoy::StatusHelpers::HasStatusCode;
using ::testing::HasSubstr;

constexpr absl::string_view kVertexClaude = R"({
  "uri_pattern": "/{version}/projects/{project}/locations/{location}/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}",
  "mutations": [
    {"body_field": "model"},
    {"body_field": "anthropic_version", "value": "vertex-2023-10-16", "keep_existing": true}
  ]
})";

LlmBindingConstSharedPtr parseOk(absl::string_view text) {
  absl::StatusOr<LlmBindingConstSharedPtr> binding = LlmBinding::parse(text);
  EXPECT_TRUE(binding.ok()) << binding.status();
  return binding.ok() ? *binding : nullptr;
}

TEST(LlmBindingParseTest, ConvertsEveryValueKind) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"mutations": [
    {"body_field": "max_tokens", "value": 4096},
    {"body_field": "temperature", "value": 0.5},
    {"body_field": "stream", "value": false},
    {"body_field": "stop", "value": null},
    {"body_field": "metadata", "value": {"user_id": "u", "tags": ["a", 1]}},
    {"body_field": "drop"}
  ]})");
  ASSERT_NE(binding, nullptr);
  EXPECT_FALSE(binding->pattern.has_value());
  ASSERT_EQ(binding->mutations.size(), 6);
  EXPECT_TRUE(binding->mutations[0].value->is_number_integer());
  EXPECT_EQ(*binding->mutations[0].value, 4096);
  EXPECT_EQ(*binding->mutations[1].value, 0.5);
  EXPECT_EQ(*binding->mutations[2].value, false);
  EXPECT_TRUE(binding->mutations[3].value->is_null());
  EXPECT_EQ(*binding->mutations[4].value,
            nlohmann::json::parse(R"({"user_id": "u", "tags": ["a", 1]})"));
  EXPECT_FALSE(binding->mutations[5].value.has_value());
}

TEST(LlmBindingParseTest, RejectsWhatCannotBeApplied) {
  for (absl::string_view text : {
           "",
           "vertex_claude",
           "/v1/{model",
           R"({"uri_pattern": "/v1/{a|b}/{c|d}"})",
           R"({"mutations": [{"value": "x"}]})",
           R"({"mutations": [{"body_field": ""}]})",
           R"({"mutations": [{"header": "bad header"}]})",
           R"({"mutations": [{"header": ":authority", "value": "x"}]})",
           R"({"mutations": [{"header": "x-a", "value": {"b": 1}}]})",
           R"({"mutations": [{"body_field": "a", "keep": true}]})",
           "{not json",
       }) {
    EXPECT_THAT(LlmBinding::parse(text), HasStatusCode(absl::StatusCode::kInvalidArgument)) << text;
  }
}

TEST(LlmBindingParseTest, EmptyPartsFallBackToTheApis) {
  const LlmBindingConstSharedPtr api =
      parseOk(R"({"uri_pattern": "/v1beta/models/{model}:{generateContent|streamGenerateContent}",)"
              R"( "mutations": [{"body_field": "model"}, {"body_field": "stream"}]})");
  const LlmBindingConstSharedPtr pattern_only = parseOk("/gw/{model}");
  const LlmBindingConstSharedPtr edits_only = parseOk(R"({"mutations": [{"header": "x-a"}]})");

  EXPECT_EQ(pattern_only->patternOver(api.get()), &*pattern_only->pattern);
  EXPECT_EQ(pattern_only->mutationsOver(api.get()).data(), api->mutations.data());
  EXPECT_EQ(edits_only->patternOver(api.get()), &*api->pattern);
  EXPECT_EQ(edits_only->mutationsOver(api.get()).data(), edits_only->mutations.data());
  EXPECT_EQ(edits_only->patternOver(nullptr), nullptr);
  EXPECT_TRUE(pattern_only->mutationsOver(nullptr).empty());
}

TEST(LlmBindingReadTest, ThePathWinsForAFieldTheBindingRemoves) {
  const LlmBindingConstSharedPtr binding = parseOk(kVertexClaude);
  nlohmann::json body = nlohmann::json::parse(R"({
    "model": "from-body", "anthropic_version": "vertex-2023-10-16", "max_tokens": 8
  })");
  std::optional<LlmBindingRead> read =
      readRequest(binding, nullptr,
                  "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
                  "claude-sonnet-4-5@20250929:streamRawPredict?x=1",
                  body);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->binding, binding);
  EXPECT_EQ(read->model, "claude-sonnet-4-5@20250929");
  EXPECT_EQ(read->path_variables.size(), 3);
  EXPECT_EQ(read->written_fields.at("anthropic_version"), "vertex-2023-10-16");
  // `stream` stays the body's: Claude on Vertex AI keeps it there.
  EXPECT_EQ(body,
            nlohmann::json::parse(
                R"({"model": "claude-sonnet-4-5@20250929", "stream": true, "max_tokens": 8})"));

  nlohmann::json unary = nlohmann::json::parse(R"({"stream": true})");
  read = readRequest(binding, nullptr,
                     "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
                     "claude-sonnet-4-5@20250929:rawPredict",
                     unary);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(unary["stream"], true);
  EXPECT_TRUE(read->written_fields.empty());
}

TEST(LlmBindingReadTest, TheBodyWinsForAFieldTheBindingKeeps) {
  const LlmBindingConstSharedPtr binding = parseOk("/gw/{model}/{chat|chat-stream}");
  nlohmann::json body = nlohmann::json::parse(R"({"model": "from-body"})");
  ASSERT_TRUE(readRequest(binding, nullptr, "/gw/from-path/chat-stream", body).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "from-body", "stream": true})"));

  // A removed `stream` takes the path's value either way.
  const LlmBindingConstSharedPtr removes_stream =
      parseOk(R"({"uri_pattern": "/gw/{model}/{chat|chat-stream}",)"
              R"( "mutations": [{"body_field": "stream"}]})");
  body = nlohmann::json::parse(R"({"stream": true})");
  ASSERT_TRUE(readRequest(removes_stream, nullptr, "/gw/m/chat", body).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "stream": false})"));
}

TEST(LlmBindingReadTest, APathThatDoesNotMatchReadsNothing) {
  const LlmBindingConstSharedPtr binding = parseOk(kVertexClaude);
  nlohmann::json body = nlohmann::json::parse(R"({"anthropic_version": "vertex-2023-10-16"})");
  const nlohmann::json original = body;
  EXPECT_FALSE(readRequest(binding, nullptr, "/v1/messages", body).has_value());
  EXPECT_EQ(body, original);
}

// A binding without a pattern reads the API's, or no path at all, and still takes out the fields
// it writes.
TEST(LlmBindingReadTest, ABindingWithoutAPatternReadsNoPath) {
  const LlmBindingConstSharedPtr edits_only = parseOk(
      R"({"mutations": [{"body_field": "anthropic_version", "value": "bedrock-2023-05-31"}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"anthropic_version": "bedrock-2023-05-31"})");
  std::optional<LlmBindingRead> read = readRequest(edits_only, nullptr, "/anything", body);
  ASSERT_TRUE(read.has_value());
  EXPECT_TRUE(read->model.empty());
  EXPECT_EQ(read->written_fields.at("anthropic_version"), "bedrock-2023-05-31");
  EXPECT_EQ(body, nlohmann::json::object());

  const LlmBindingConstSharedPtr api = parseOk("/v1beta/models/{model}:generateContent");
  body = nlohmann::json::object();
  EXPECT_FALSE(readRequest(edits_only, api.get(), "/anything", body).has_value());
  read = readRequest(edits_only, api.get(), "/v1beta/models/m:generateContent", body);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m"})"));

  // A body that is not an object is matched, but not edited.
  nlohmann::json array = nlohmann::json::array();
  EXPECT_TRUE(readRequest(api, nullptr, "/v1beta/models/m:generateContent", array).has_value());
  EXPECT_EQ(array, nlohmann::json::array());
}

TEST(LlmBindingWriteTest, RendersFromTheBodyThenTheClientsPath) {
  const LlmBindingConstSharedPtr binding =
      parseOk("/{version}/projects/{project}/locations/{location}/publishers/google/models/"
              "{model}:{generateContent|streamGenerateContent?alt=sse}");
  const LlmBindingRead arrived{nullptr,
                               "claude-sonnet-4-5",
                               {{"version", "v1"}, {"project", "acme"}, {"location", "us-east5"}},
                               {}};
  nlohmann::json body =
      nlohmann::json::parse(R"({"model": "gemini-2.5-flash", "stream": true, "project": "p2"})");
  absl::StatusOr<LlmBindingWrite> write = writeRequest(*binding, nullptr, &arrived, body);
  ASSERT_TRUE(write.ok()) << write.status();
  EXPECT_EQ(write->path, "/v1/projects/p2/locations/us-east5/publishers/google/models/"
                         "gemini-2.5-flash:streamGenerateContent?alt=sse");
  EXPECT_TRUE(write->headers.empty());

  EXPECT_THAT(writeRequest(*binding, nullptr, nullptr, body),
              HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("`{version}`")));

  for (const char* model : {R"("../x")", R"("a/b")", "1", "null"}) {
    nlohmann::json bad = nlohmann::json::parse(R"({"stream": false})");
    bad["model"] = nlohmann::json::parse(model);
    EXPECT_THAT(writeRequest(*binding, nullptr, &arrived, bad),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("model id")))
        << model;
  }
  nlohmann::json no_model = nlohmann::json::object();
  EXPECT_THAT(writeRequest(*binding, nullptr, &arrived, no_model),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(LlmBindingWriteTest, AppliesTheMutationsInOrder) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"mutations": [
    {"body_field": "model"},
    {"body_field": "max_tokens", "value": 4096, "keep_existing": true},
    {"body_field": "top_k", "value": 40, "keep_existing": true},
    {"body_field": "temperature", "value": 0.5},
    {"body_field": "labels", "value": {"tenant": "{project}"}},
    {"body_field": "user", "value": "{project}-{model}"},
    {"header": "x-llm-model", "value": "{model}"},
    {"header": "anthropic-version", "value": "2023-06-01", "keep_existing": true},
    {"header": "x-api-key"}
  ]})");
  const LlmBindingRead arrived{nullptr, "", {{"project", "acme"}}, {}};
  nlohmann::json body =
      nlohmann::json::parse(R"({"model": "claude-haiku-4-5", "max_tokens": 16, "temperature": 1})");
  absl::StatusOr<LlmBindingWrite> write = writeRequest(*binding, nullptr, &arrived, body);
  ASSERT_TRUE(write.ok()) << write.status();
  EXPECT_FALSE(write->path.has_value());
  // `{model}` reads the body as it was before `model` was removed.
  EXPECT_EQ(body, nlohmann::json::parse(R"({
    "max_tokens": 16, "top_k": 40, "temperature": 0.5, "labels": {"tenant": "{project}"},
    "user": "acme-claude-haiku-4-5"
  })"));
  ASSERT_EQ(write->headers.size(), 3);
  EXPECT_EQ(write->headers[0].name, "x-llm-model");
  EXPECT_EQ(write->headers[0].value, "claude-haiku-4-5");
  EXPECT_FALSE(write->headers[0].keep_existing);
  EXPECT_EQ(write->headers[1].value, "2023-06-01");
  EXPECT_TRUE(write->headers[1].keep_existing);
  EXPECT_FALSE(write->headers[2].value.has_value());
}

TEST(LlmBindingWriteTest, RefusesAPlaceholderItCannotFill) {
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m", "path": "../x"})");
  EXPECT_THAT(writeRequest(*parseOk(R"({"mutations": [{"header": "x-a", "value": "{tenant}"}]})"),
                           nullptr, nullptr, body),
              HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("`{tenant}`")));
  EXPECT_THAT(writeRequest(*parseOk(R"({"mutations": [{"header": "x-a", "value": "{path}"}]})"),
                           nullptr, nullptr, body),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  // An unclosed brace is text.
  absl::StatusOr<LlmBindingWrite> write = writeRequest(
      *parseOk(R"({"mutations": [{"header": "x-a", "value": "a{b"}]})"), nullptr, nullptr, body);
  ASSERT_TRUE(write.ok());
  EXPECT_EQ(write->headers[0].value, "a{b");
}

// Leaving in the binding it arrived in, a request gets back the fields the binding writes as the
// client sent them; one the client left out takes the binding's value.
TEST(LlmBindingWriteTest, TheSameBindingRestoresWhatTheClientSent) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({
    "uri_pattern": "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}",
    "mutations": [
      {"body_field": "model"},
      {"body_field": "anthropic_version", "value": "vertex-2023-10-16"},
      {"body_field": "service_tier", "value": "auto"}
    ]
  })");
  const LlmBindingRead arrived{binding, "m", {}, {{"anthropic_version", "vertex-2099-01-01"}}};
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
  ASSERT_TRUE(writeRequest(*binding, nullptr, &arrived, body).ok());
  EXPECT_EQ(body, nlohmann::json::parse(
                      R"({"anthropic_version": "vertex-2099-01-01", "service_tier": "auto"})"));

  // Another binding writes its own values.
  const LlmBindingConstSharedPtr other = parseOk(kVertexClaude);
  const LlmBindingRead elsewhere{other, "m", {}, {{"anthropic_version", "vertex-2099-01-01"}}};
  body = nlohmann::json::parse(R"({"model": "m"})");
  ASSERT_TRUE(writeRequest(*binding, nullptr, &elsewhere, body).ok());
  EXPECT_EQ(body["anthropic_version"], "vertex-2023-10-16");
}

TEST(LlmBindingWriteTest, EditsOnlyAnObjectBody) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"mutations": [{"body_field": "a", "value": 1}, {"header": "x-a", "value": "b"}]})");
  nlohmann::json array = nlohmann::json::array();
  absl::StatusOr<LlmBindingWrite> write = writeRequest(*binding, nullptr, nullptr, array);
  ASSERT_TRUE(write.ok());
  EXPECT_EQ(array, nlohmann::json::array());
  EXPECT_EQ(write->headers.size(), 1);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
