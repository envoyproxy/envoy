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
using ::Envoy::StatusHelpers::IsOk;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::HasSubstr;

constexpr absl::string_view VertexClaude = R"({
  "uri_pattern": "/{version}/projects/{project}/locations/{location}/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}",
  "mutations": [
    {"body_field": "model"},
    {"body_field": "anthropic_version", "value": "vertex-2023-10-16", "keep_existing": true}
  ]
})";
constexpr absl::string_view VertexGemini =
    "/{version}/projects/{project}/locations/{location}/publishers/google/models/"
    "{model}:{generateContent|streamGenerateContent?alt=sse}";
constexpr absl::string_view GeminiApi =
    R"({"uri_pattern": "/v1beta/models/{model}:{generateContent|streamGenerateContent}",)"
    R"( "mutations": [{"body_field": "model"}, {"body_field": "stream"}]})";

LlmBindingConstSharedPtr parseOk(absl::string_view text) {
  absl::StatusOr<LlmBindingConstSharedPtr> binding = LlmBinding::parse(text);
  EXPECT_TRUE(binding.ok()) << binding.status();
  return binding.ok() ? *binding : nullptr;
}

std::optional<LlmBindingRead> read(const LlmBindingConstSharedPtr& binding, absl::string_view path,
                                   nlohmann::json& body,
                                   const LlmBindingConstSharedPtr& api_binding = nullptr) {
  return readClientPath(binding, api_binding, {}, path, body);
}

absl::StatusOr<std::optional<std::string>> write(const LlmBinding& binding,
                                                 const LlmBindingRead* arrived,
                                                 nlohmann::json& body,
                                                 const LlmBinding* api_binding = nullptr) {
  bool unresolved = false;
  return writeRequest(binding, api_binding, arrived, body, unresolved);
}

TEST(LlmBindingParseTest, ConvertsEveryValueKind) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"mutations": [
    {"body_field": "max_tokens", "value": 4096},
    {"body_field": "temperature", "value": 0.5},
    {"body_field": "stream", "value": false},
    {"body_field": "metadata", "value": {"user_id": "u", "tags": ["a", 1]}},
    {"body_field": "drop"}
  ]})");
  ASSERT_NE(binding, nullptr);
  EXPECT_FALSE(binding->pattern().has_value());
  const std::vector<LlmBinding::Mutation>& mutations = binding->mutations();
  ASSERT_EQ(mutations.size(), 5);
  EXPECT_TRUE(mutations[0].value->is_number_integer());
  EXPECT_EQ(*mutations[0].value, 4096);
  EXPECT_EQ(*mutations[1].value, 0.5);
  EXPECT_EQ(*mutations[2].value, false);
  EXPECT_EQ(*mutations[3].value, nlohmann::json::parse(R"({"user_id": "u", "tags": ["a", 1]})"));
  EXPECT_FALSE(mutations[4].value.has_value());
  EXPECT_FALSE(binding->removesModel());
}

// A null value removes the field, as an unset one does.
TEST(LlmBindingParseTest, NullValueRemovesTheField) {
  const LlmBindingConstSharedPtr binding =
      parseOk(R"({"mutations": [{"body_field": "model", "value": null}]})");
  ASSERT_EQ(binding->mutations().size(), 1);
  EXPECT_FALSE(binding->mutations()[0].value.has_value());
  EXPECT_TRUE(binding->removesModel());
}

TEST(LlmBindingParseTest, RejectsWhatCannotBeApplied) {
  for (absl::string_view text : {
           "",
           "vertex_claude",
           "/v1/{model",
           R"({"uri_pattern": "/v1/{a|b}/{c|d}"})",
           R"({"uri_pattern": "/v1/{model}/{rest=**}"})",
           R"({"mutations": [{"value": "x"}]})",
           R"({"mutations": [{"body_field": ""}]})",
           R"({"mutations": [{"header": "x-a", "value": "b"}]})",
           R"({"mutations": [{"body_field": "a", "keep": true}]})",
           "{not json",
       }) {
    EXPECT_THAT(LlmBinding::parse(text), HasStatusCode(absl::StatusCode::kInvalidArgument)) << text;
  }
}

// A YAML block scalar leaves a newline after the value.
TEST(LlmBindingParseTest, IgnoresWhitespaceAroundTheValue) {
  EXPECT_EQ(parseOk(" /v1/{model}\n")->pattern()->source(), "/v1/{model}");
  EXPECT_EQ(parseOk("\n{\"uri_pattern\": \"/v1/{model}\"}\n")->pattern()->source(), "/v1/{model}");
}

TEST(LlmBindingParseTest, KnowsWhatItRemoves) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"mutations": [
    {"body_field": "model"}, {"body_field": "stream"}, {"body_field": "labels", "value": "x"}
  ]})");
  EXPECT_TRUE(binding->removesModel());
  EXPECT_TRUE(binding->removesStream());
}

// A binding's empty part is the API's: a pattern-only binding takes the API's mutations, and a
// mutations-only one its pattern.
TEST(LlmBindingTest, EmptyPartsFallBackToTheApis) {
  const LlmBindingConstSharedPtr api = parseOk(GeminiApi);
  const LlmBindingConstSharedPtr pattern_only = parseOk("/gw/{model}");
  const LlmBindingConstSharedPtr edits_only =
      parseOk(R"({"mutations": [{"body_field": "x", "value": 1}]})");

  // The API removes `model`, so the path's replaces the body's.
  nlohmann::json body = nlohmann::json::parse(R"({"model": "from-body"})");
  ASSERT_TRUE(read(pattern_only, "/gw/m", body, api).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m"})"));
  EXPECT_THAT(write(*pattern_only, nullptr, body, api.get()),
              IsOkAndHolds(std::make_optional<std::string>("/gw/m")));
  EXPECT_EQ(body, nlohmann::json::object());

  body = nlohmann::json::parse(R"({"model": "m", "stream": true})");
  EXPECT_THAT(
      write(*edits_only, nullptr, body, api.get()),
      IsOkAndHolds(std::make_optional<std::string>("/v1beta/models/m:streamGenerateContent")));
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "stream": true, "x": 1})"));
}

TEST(LlmBindingReadTest, ThePathWinsForAFieldTheBindingRemoves) {
  const LlmBindingConstSharedPtr binding = parseOk(VertexClaude);
  nlohmann::json body = nlohmann::json::parse(R"({
    "model": "from-body", "anthropic_version": "vertex-2023-10-16", "max_tokens": 8
  })");
  std::optional<LlmBindingRead> arrived =
      read(binding,
           "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
           "claude-sonnet-4-5@20250929:streamRawPredict?x=1",
           body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(arrived->binding, binding);
  EXPECT_EQ(arrived->model, "claude-sonnet-4-5@20250929");
  EXPECT_EQ(arrived->path_variables.size(), 3);
  EXPECT_EQ(arrived->written_fields.at("anthropic_version"), "vertex-2023-10-16");
  // `stream` stays the body's: Claude on Vertex AI keeps it there.
  EXPECT_EQ(body,
            nlohmann::json::parse(
                R"({"model": "claude-sonnet-4-5@20250929", "stream": true, "max_tokens": 8})"));

  nlohmann::json unary = nlohmann::json::parse(R"({"stream": true})");
  arrived = read(binding,
                 "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
                 "claude-sonnet-4-5@20250929:rawPredict",
                 unary);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(unary["stream"], true);
  EXPECT_TRUE(arrived->written_fields.empty());
}

TEST(LlmBindingReadTest, TheBodyWinsForAFieldTheBindingKeeps) {
  const LlmBindingConstSharedPtr binding = parseOk("/gw/{model}/{chat|chat-stream}");
  nlohmann::json body = nlohmann::json::parse(R"({"model": "from-body"})");
  ASSERT_TRUE(read(binding, "/gw/from-path/chat-stream", body).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "from-body", "stream": true})"));

  // A removed `stream` takes the path's value either way.
  const LlmBindingConstSharedPtr removes_stream =
      parseOk(R"({"uri_pattern": "/gw/{model}/{chat|chat-stream}",)"
              R"( "mutations": [{"body_field": "stream"}]})");
  body = nlohmann::json::parse(R"({"stream": true})");
  ASSERT_TRUE(read(removes_stream, "/gw/m/chat", body).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "stream": false})"));
}

// A path that does not name the streaming mode leaves the body's, even under an API binding that
// removes `stream`.
TEST(LlmBindingReadTest, ThePathLeavesAModeItDoesNotNameToTheBody) {
  const LlmBindingConstSharedPtr api = parseOk(GeminiApi);
  for (absl::string_view pattern : {"/gw/{model}", "/gw/{model}{|?alt=sse}"}) {
    SCOPED_TRACE(pattern);
    nlohmann::json body = nlohmann::json::parse(R"({"stream": true})");
    ASSERT_TRUE(read(parseOk(pattern), "/gw/m?alt=sse", body, api).has_value());
    EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "stream": true})"));
  }
}

TEST(LlmBindingReadTest, APathThatDoesNotMatchReadsNothing) {
  const LlmBindingConstSharedPtr binding = parseOk(VertexClaude);
  nlohmann::json body = nlohmann::json::parse(R"({"anthropic_version": "vertex-2023-10-16"})");
  const nlohmann::json original = body;
  EXPECT_FALSE(read(binding, "/v1/messages", body).has_value());
  EXPECT_EQ(body, original);
}

// A binding without a pattern reads the API's, or no path at all, and still takes out the fields
// it writes.
TEST(LlmBindingReadTest, ABindingWithoutAPatternReadsNoPath) {
  const LlmBindingConstSharedPtr edits_only = parseOk(
      R"({"mutations": [{"body_field": "anthropic_version", "value": "bedrock-2023-05-31"}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"anthropic_version": "bedrock-2023-05-31"})");
  std::optional<LlmBindingRead> arrived = read(edits_only, "/anything", body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_TRUE(arrived->model.empty());
  EXPECT_EQ(arrived->written_fields.at("anthropic_version"), "bedrock-2023-05-31");
  EXPECT_EQ(body, nlohmann::json::object());

  const LlmBindingConstSharedPtr api = parseOk("/v1beta/models/{model}:generateContent");
  body = nlohmann::json::object();
  EXPECT_FALSE(read(edits_only, "/anything", body, api).has_value());
  arrived = read(edits_only, "/v1beta/models/m:generateContent", body, api);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m"})"));

  // A body that is not an object is matched, but not edited.
  nlohmann::json array = nlohmann::json::array();
  EXPECT_TRUE(read(api, "/v1beta/models/m:generateContent", array).has_value());
  EXPECT_EQ(array, nlohmann::json::array());
}

// The binding given replaces the others; without one, the API's own is tried, then its client
// bindings, in order.
TEST(LlmBindingReadTest, ReadsTheClientPathUnderTheFirstBindingThatMatches) {
  const LlmBindingConstSharedPtr own = parseOk("/v1beta/models/{model}:generateContent");
  const LlmBindingConstSharedPtr vertex = parseOk(VertexGemini);
  const std::vector<LlmBindingConstSharedPtr> clients{vertex};
  constexpr absl::string_view vertex_path =
      "/v1/projects/p/locations/l/publishers/google/models/g:generateContent";

  nlohmann::json body = nlohmann::json::object();
  std::optional<LlmBindingRead> arrived = readClientPath(nullptr, own, clients, vertex_path, body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(arrived->binding, vertex);
  arrived = readClientPath(nullptr, own, clients, "/v1beta/models/g:generateContent", body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(arrived->binding, own);
  EXPECT_FALSE(readClientPath(nullptr, own, clients, "/v1/messages", body).has_value());

  const LlmBindingConstSharedPtr gateway = parseOk("/gw/{model}");
  EXPECT_FALSE(readClientPath(gateway, own, clients, vertex_path, body).has_value());
  EXPECT_EQ(readClientPath(gateway, own, clients, "/gw/m", body)->model, "m");
}

// `{model}` is the body's; every other variable is the client's path's, even when the body has a
// field of that name.
TEST(LlmBindingWriteTest, RendersTheModelFromTheBodyAndTheRestFromThePath) {
  const LlmBindingConstSharedPtr binding = parseOk(VertexGemini);
  const LlmBindingRead arrived{nullptr,
                               "claude-sonnet-4-5",
                               {{"version", "v1"}, {"project", "acme"}, {"location", "us-east5"}},
                               {}};
  nlohmann::json body =
      nlohmann::json::parse(R"({"model": "gemini-2.5-flash", "stream": true, "project": "p2"})");
  EXPECT_THAT(write(*binding, &arrived, body),
              IsOkAndHolds(std::make_optional<std::string>(
                  "/v1/projects/acme/locations/us-east5/publishers/google/models/"
                  "gemini-2.5-flash:streamGenerateContent?alt=sse")));

  // A model that cannot name a path segment is the request's fault.
  for (const char* model : {R"("../x")", R"("a/b")", "1", "null"}) {
    nlohmann::json bad = nlohmann::json::parse(R"({"stream": false})");
    bad["model"] = nlohmann::json::parse(model);
    bool unresolved = true;
    EXPECT_THAT(writeRequest(*binding, nullptr, &arrived, bad, unresolved),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("model id")))
        << model;
    EXPECT_FALSE(unresolved) << model;
  }
  nlohmann::json no_model = nlohmann::json::object();
  EXPECT_THAT(write(*binding, &arrived, no_model),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  // Without a pattern there is no path to render.
  EXPECT_THAT(write(*parseOk(R"({"mutations": [{"body_field": "a"}]})"), nullptr, body),
              IsOkAndHolds(std::nullopt));
}

// A value the client's path did not hold, or an unnamed wildcard, which has none, is the proxy's
// configuration at fault rather than the request.
TEST(LlmBindingWriteTest, FlagsAValueTheClientsPathDidNotHold) {
  const LlmBindingRead arrived{
      nullptr, "", {{"version", "v1"}, {"project", "acme"}, {"location", "us-east5"}}, {}};
  const auto expect_unresolved = [](const LlmBinding& binding, const LlmBindingRead* from,
                                    absl::string_view message) {
    nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
    bool unresolved = false;
    EXPECT_THAT(writeRequest(binding, nullptr, from, body, unresolved),
                HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr(message)));
    EXPECT_TRUE(unresolved) << message;
  };
  const LlmBindingConstSharedPtr vertex = parseOk(VertexGemini);
  expect_unresolved(*vertex, nullptr, "`{version}`");
  expect_unresolved(*parseOk(absl::StrCat("/v1/", std::string(1, '*'), "/{model}")), &arrived,
                    "wildcard");
  expect_unresolved(*parseOk(R"({"mutations": [{"body_field": "labels", "value": "{tenant}"}]})"),
                    &arrived, "`{tenant}`");

  nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
  bool unresolved = true;
  EXPECT_THAT(writeRequest(*vertex, nullptr, &arrived, body, unresolved), IsOk());
  EXPECT_FALSE(unresolved);
}

TEST(LlmBindingWriteTest, AppliesTheMutationsInOrder) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"mutations": [
    {"body_field": "model"},
    {"body_field": "max_tokens", "value": 4096, "keep_existing": true},
    {"body_field": "top_k", "value": 40, "keep_existing": true},
    {"body_field": "temperature", "value": 0.5},
    {"body_field": "labels", "value": {"tenant": "{project}"}},
    {"body_field": "user", "value": "{project}-{model}"}
  ]})");
  const LlmBindingRead arrived{nullptr, "", {{"project", "acme"}}, {}};
  nlohmann::json body =
      nlohmann::json::parse(R"({"model": "claude-haiku-4-5", "max_tokens": 16, "temperature": 1})");
  ASSERT_THAT(write(*binding, &arrived, body), IsOkAndHolds(std::nullopt));
  // `{model}` reads the body as it was before `model` was removed.
  EXPECT_EQ(body, nlohmann::json::parse(R"({
    "max_tokens": 16, "top_k": 40, "temperature": 0.5, "labels": {"tenant": "{project}"},
    "user": "acme-claude-haiku-4-5"
  })"));
}

// A mutation that keeps the body's field writes nothing, so it needs no values.
TEST(LlmBindingWriteTest, KeepingAFieldNeedsNoValues) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"mutations": [{"body_field": "x", "value": "{tenant}", "keep_existing": true}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"x": "existing"})");
  const nlohmann::json original = body;
  ASSERT_THAT(write(*binding, nullptr, body), IsOkAndHolds(std::nullopt));
  EXPECT_EQ(body, original);
}

// A placeholder fills a JSON string, so any value the request has will do.
TEST(LlmBindingWriteTest, FillsAPlaceholderWithAnyValueTheRequestHas) {
  nlohmann::json body = nlohmann::json::parse(R"({"model": "ibm/granite-3-8b-instruct"})");
  ASSERT_THAT(write(*parseOk(R"({"mutations": [{"body_field": "model_id", "value": "{model}"}]})"),
                    nullptr, body),
              IsOk());
  EXPECT_EQ(body["model_id"], "ibm/granite-3-8b-instruct");
  EXPECT_THAT(write(*parseOk(R"({"mutations": [{"body_field": "x", "value": "{tenant}"}]})"),
                    nullptr, body),
              HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("`{tenant}`")));
  // An unclosed brace is text.
  ASSERT_THAT(
      write(*parseOk(R"({"mutations": [{"body_field": "x", "value": "a{b"}]})"), nullptr, body),
      IsOk());
  EXPECT_EQ(body["x"], "a{b");
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
  ASSERT_THAT(write(*binding, &arrived, body), IsOk());
  EXPECT_EQ(body, nlohmann::json::parse(
                      R"({"anthropic_version": "vertex-2099-01-01", "service_tier": "auto"})"));

  // Another binding writes its own values.
  const LlmBindingConstSharedPtr other = parseOk(VertexClaude);
  const LlmBindingRead elsewhere{other, "m", {}, {{"anthropic_version", "vertex-2099-01-01"}}};
  body = nlohmann::json::parse(R"({"model": "m"})");
  ASSERT_THAT(write(*binding, &elsewhere, body), IsOk());
  EXPECT_EQ(body["anthropic_version"], "vertex-2023-10-16");
}

// A restored field needs none of the values its default would.
TEST(LlmBindingWriteTest, ARestoredFieldNeedsNoValues) {
  const LlmBindingConstSharedPtr binding =
      parseOk(R"({"mutations": [{"body_field": "tenant", "value": "{project}"}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m", "tenant": "client-tenant"})");
  const nlohmann::json original = body;
  const std::optional<LlmBindingRead> arrived = read(binding, "/v1/messages", body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_FALSE(body.contains("tenant"));
  ASSERT_THAT(write(*binding, &*arrived, body), IsOkAndHolds(std::nullopt));
  EXPECT_EQ(body, original);
}

TEST(LlmBindingWriteTest, EditsOnlyAnObjectBody) {
  const LlmBindingConstSharedPtr binding =
      parseOk(R"({"mutations": [{"body_field": "a", "value": 1}]})");
  nlohmann::json array = nlohmann::json::array();
  ASSERT_THAT(write(*binding, nullptr, array), IsOk());
  EXPECT_EQ(array, nlohmann::json::array());
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
