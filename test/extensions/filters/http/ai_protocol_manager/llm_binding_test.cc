#include <optional>
#include <string>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include "test/test_common/status_utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
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
  "body_mutations": [{"remove": "model"}, {"append": {"record": {"key": "anthropic_version", "value": "vertex-2023-10-16"}, "action": "ADD_IF_ABSENT"}}], "header_mutations": [{"remove": "anthropic-version"}]
})";
constexpr absl::string_view VertexGemini =
    "/{version}/projects/{project}/locations/{location}/publishers/google/models/"
    "{model}:{generateContent|streamGenerateContent?alt=sse}";
constexpr absl::string_view GeminiApi =
    R"({"uri_pattern": "/v1beta/models/{model}:{generateContent|streamGenerateContent}",)"
    R"( "body_mutations": [{"remove": "model"}, {"remove": "stream"}]})";

LlmBindingConstSharedPtr parseOk(absl::string_view text) {
  absl::StatusOr<LlmBindingConstSharedPtr> binding = LlmBinding::parse(text);
  EXPECT_TRUE(binding.ok()) << binding.status();
  return binding.ok() ? *binding : nullptr;
}

// The request's headers, for a test that has some; a request without any by default.
using Headers = absl::flat_hash_map<std::string, std::string>;
HeaderLookup lookup(const Headers& headers = {}) {
  return [headers](absl::string_view name) -> std::optional<std::string> {
    const auto found = headers.find(name);
    return found == headers.end() ? std::nullopt : std::make_optional(found->second);
  };
}

std::optional<LlmBindingRead> read(const LlmBindingConstSharedPtr& binding, absl::string_view path,
                                   nlohmann::json& body,
                                   const LlmBindingConstSharedPtr& api_binding = nullptr,
                                   const Headers& headers = {}) {
  return readClientPath(binding, api_binding, {}, path, body, lookup(headers));
}

absl::StatusOr<std::optional<std::string>>
write(const LlmBinding& binding, const LlmBindingRead* arrived, nlohmann::json& body,
      const LlmBinding* api_binding = nullptr, std::vector<HeaderMutation>* headers = nullptr,
      const Headers& sent = {}) {
  bool unresolved = false;
  std::vector<HeaderMutation> discarded;
  return writeRequest(binding, api_binding, arrived, body, lookup(sent),
                      headers != nullptr ? *headers : discarded, unresolved);
}

// `headers` as `name: value` lines, with `name` alone for a removal, for one-line comparisons.
std::vector<std::string> describe(const std::vector<HeaderMutation>& headers) {
  std::vector<std::string> lines;
  for (const HeaderMutation& header : headers) {
    lines.push_back(header.value.has_value() ? absl::StrCat(header.name, ": ", *header.value)
                                             : header.name);
  }
  return lines;
}

// As `write`, reporting `unresolved`, for the tests about whose fault a failure is.
absl::StatusOr<std::optional<std::string>> writeUnresolved(const LlmBinding& binding,
                                                           const LlmBindingRead* arrived,
                                                           nlohmann::json& body, bool& unresolved) {
  std::vector<HeaderMutation> discarded;
  return writeRequest(binding, nullptr, arrived, body, lookup(), discarded, unresolved);
}

TEST(LlmBindingParseTest, ConvertsEveryValueKind) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"body_mutations": [
    {"append": {"record": {"key": "max_tokens", "value": 4096}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}},
    {"append": {"record": {"key": "temperature", "value": 0.5}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}},
    {"append": {"record": {"key": "stream", "value": false}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}},
    {"append": {"record": {"key": "metadata", "value": {"user_id": "u", "tags": ["a", 1]}}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}},
    {"remove": "drop"}
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

// `remove` removes the field; `append` with no value is rejected rather than read as a removal.
TEST(LlmBindingParseTest, RemoveRemovesTheField) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({"body_mutations": [{"remove": "model"}]})");
  ASSERT_EQ(binding->mutations().size(), 1);
  EXPECT_FALSE(binding->mutations()[0].value.has_value());
  EXPECT_TRUE(binding->removesModel());
}

TEST(LlmBindingParseTest, RejectsWhatCannotBeApplied) {
  for (
      absl::string_view text : {
          "",
          "vertex_claude",
          "/v1/{model",
          R"({"uri_pattern": "/v1/{a|b}/{c|d}"})",
          R"({"uri_pattern": "/v1/{model}/{rest=**}"})",
          // A mutation names its target.
          R"({"body_mutations": [{"append": {"record": {"value": "x"}}}]})",
          R"({"body_mutations": [{"remove": ""}]})",
          R"({"header_mutations": [{"append": {"header": {"value": "v"}}}]})",
          R"({"header_mutations": [{"remove": ""}]})",
          // And is one action: `append` sets, `remove` removes.
          R"({"body_mutations": [{"append": {"record": {"key": "a", "value": null}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"body_mutations": [{"append": {"record": {"key": "a", "value": 1}}, "remove": "b"}]})",
          R"({"header_mutations": [{"remove_on_match": {"key_matcher": {"exact": "x-a"}}}]})",
          // Rejected: an unnamed action (the enum default), OVERWRITE_IF_EXISTS, `raw_value`, and
          // the deprecated `entry` and `append` fields.
          R"({"body_mutations": [{"append": {"record": {"key": "a", "value": 1}}}]})",
          R"({"body_mutations": [{"append": {"record": {"key": "a", "value": 1}, "action": "OVERWRITE_IF_EXISTS"}}]})",
          R"({"body_mutations": [{"append": {"entry": {"key": "a", "value": "MQ=="}, "record": {"key": "a", "value": 1}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "v"}}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x-a", "raw_value": "dg=="}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "v"}, "append": false, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          // Headers Envoy frames itself, and the pseudo-headers.
          R"({"header_mutations": [{"append": {"header": {"key": ":path", "value": "/x"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "host", "value": "h"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "content-length", "value": "0"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"remove": "transfer-encoding"}]})",
          // Header names are matched lower-case.
          R"({"header_mutations": [{"append": {"header": {"key": "Anthropic-Version", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          // And are RFC 7230 tokens: the filter-state path is not PGV-checked.
          R"({"header_mutations": [{"append": {"header": {"key": "x@a", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x a", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x:a", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"header_mutations": [{"append": {"header": {"key": "x/a", "value": "v"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})",
          R"({"body_mutations": [{"append": {"record": {"key": "a", "value": 1}, "keep": true}}]})",
          "{not json",
      }) {
    EXPECT_THAT(LlmBinding::parse(text), HasStatusCode(absl::StatusCode::kInvalidArgument)) << text;
  }
}

// Header mutations come before body mutations, each list in its own order.
TEST(LlmBindingParseTest, ConvertsHeaderMutations) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({
    "body_mutations": [{"remove": "model"}],
    "header_mutations": [
      {"append": {"header": {"key": "anthropic-version", "value": "2023-06-01"}, "append_action": "ADD_IF_ABSENT"}},
      {"append": {"header": {"key": "x-tenant", "value": "{project}"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}},
      {"remove": "x-drop"}
    ]
  })");
  ASSERT_NE(binding, nullptr);
  const std::vector<LlmBinding::Mutation>& mutations = binding->mutations();
  ASSERT_EQ(mutations.size(), 4);
  EXPECT_TRUE(mutations[0].isHeader());
  EXPECT_EQ(mutations[0].header, "anthropic-version");
  EXPECT_EQ(*mutations[0].value, "2023-06-01");
  EXPECT_TRUE(mutations[0].keep_existing);
  EXPECT_EQ(*mutations[1].value, "{project}");
  EXPECT_FALSE(mutations[1].keep_existing);
  EXPECT_FALSE(mutations[2].value.has_value());
  EXPECT_FALSE(mutations[3].isHeader());
  // Removing a header named `model` is not removing the body's model.
  EXPECT_TRUE(binding->removesModel());
  EXPECT_FALSE(parseOk(R"({"header_mutations": [{"remove": "model"}]})")->removesModel());
}

// A YAML block scalar leaves a newline after the value.
TEST(LlmBindingParseTest, IgnoresWhitespaceAroundTheValue) {
  EXPECT_EQ(parseOk(" /v1/{model}\n")->pattern()->source(), "/v1/{model}");
  EXPECT_EQ(parseOk("\n{\"uri_pattern\": \"/v1/{model}\"}\n")->pattern()->source(), "/v1/{model}");
}

TEST(LlmBindingParseTest, KnowsWhatItRemoves) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"body_mutations": [{"remove": "model"}, {"remove": "stream"}, {"append": {"record": {"key": "labels", "value": "x"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  EXPECT_TRUE(binding->removesModel());
  EXPECT_TRUE(binding->removesStream());
}

// A binding's empty pattern is the API's, and its mutations apply on top of the API's own.
TEST(LlmBindingTest, EmptyPartsFallBackToTheApis) {
  const LlmBindingConstSharedPtr api = parseOk(GeminiApi);
  const LlmBindingConstSharedPtr pattern_only = parseOk("/gw/{model}");
  const LlmBindingConstSharedPtr edits_only = parseOk(
      R"({"body_mutations": [{"append": {"record": {"key": "x", "value": 1}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");

  // The API removes `model`, so the path's replaces the body's.
  nlohmann::json body = nlohmann::json::parse(R"({"model": "from-body"})");
  ASSERT_TRUE(read(pattern_only, "/gw/m", body, api).has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m"})"));
  EXPECT_THAT(write(*pattern_only, nullptr, body, api.get()),
              IsOkAndHolds(std::make_optional<std::string>("/gw/m")));
  EXPECT_EQ(body, nlohmann::json::object());

  // The API's removals of `model` and `stream` still apply beside the binding's own edit.
  body = nlohmann::json::parse(R"({"model": "m", "stream": true})");
  EXPECT_THAT(
      write(*edits_only, nullptr, body, api.get()),
      IsOkAndHolds(std::make_optional<std::string>("/v1beta/models/m:streamGenerateContent")));
  EXPECT_EQ(body, nlohmann::json::parse(R"({"x": 1})"));
}

// A binding's mutations add to the API's rather than replace them: a header on top of Gemini's
// binding does not undo its body edits, on the way out or on the way in.
TEST(LlmBindingTest, MutationsAddToTheApis) {
  const LlmBindingConstSharedPtr api = parseOk(GeminiApi);
  const LlmBindingConstSharedPtr header_only = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "x-goog-user-project", "value": "{project}"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  const LlmBindingRead arrived{nullptr, "", {{"project", "acme"}}, {}, {}};

  nlohmann::json body = nlohmann::json::parse(R"({"model": "m", "stream": false})");
  std::vector<HeaderMutation> headers;
  EXPECT_THAT(write(*header_only, &arrived, body, api.get(), &headers),
              IsOkAndHolds(std::make_optional<std::string>("/v1beta/models/m:generateContent")));
  EXPECT_EQ(body, nlohmann::json::object());
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("x-goog-user-project: acme"));

  // Coming in, the API's removals still put the path's model and streaming mode into the body,
  // and the binding's header is recorded as the API's would be.
  body = nlohmann::json::parse(R"({"model": "from-body"})");
  const std::optional<LlmBindingRead> read_back = read(
      header_only, "/v1beta/models/m:generateContent", body, api, {{"x-goog-user-project", "p"}});
  ASSERT_TRUE(read_back.has_value());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "stream": false})"));
  EXPECT_THAT(read_back->written_headers,
              ::testing::ElementsAre(::testing::Pair("x-goog-user-project", "p")));
}

// A binding overrides one of the API's edits by naming the same field or header; the API's
// other edits still apply.
TEST(LlmBindingTest, ABindingOverridesAnApiEditByNamingItsTarget) {
  const LlmBindingConstSharedPtr api = parseOk(GeminiApi);
  // Keeps `model` in the body (the API would remove it); `stream` is still removed.
  const LlmBindingConstSharedPtr keeps_model = parseOk(
      R"({"body_mutations": [{"append": {"record": {"key": "model", "value": "{model}"}, "action": "ADD_IF_ABSENT"}}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m", "stream": true})");
  EXPECT_THAT(
      write(*keeps_model, nullptr, body, api.get()),
      IsOkAndHolds(std::make_optional<std::string>("/v1beta/models/m:streamGenerateContent")));
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m"})"));
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
              R"( "body_mutations": [{"remove": "stream"}]})");
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
      R"({"body_mutations": [{"append": {"record": {"key": "anthropic_version", "value": "bedrock-2023-05-31"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
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
  std::optional<LlmBindingRead> arrived =
      readClientPath(nullptr, own, clients, vertex_path, body, lookup());
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(arrived->binding, vertex);
  arrived =
      readClientPath(nullptr, own, clients, "/v1beta/models/g:generateContent", body, lookup());
  ASSERT_TRUE(arrived.has_value());
  EXPECT_EQ(arrived->binding, own);
  EXPECT_FALSE(readClientPath(nullptr, own, clients, "/v1/messages", body, lookup()).has_value());

  const LlmBindingConstSharedPtr gateway = parseOk("/gw/{model}");
  EXPECT_FALSE(readClientPath(gateway, own, clients, vertex_path, body, lookup()).has_value());
  EXPECT_EQ(readClientPath(gateway, own, clients, "/gw/m", body, lookup())->model, "m");
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
    EXPECT_THAT(writeUnresolved(*binding, &arrived, bad, unresolved),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("model id")))
        << model;
    EXPECT_FALSE(unresolved) << model;
  }
  nlohmann::json no_model = nlohmann::json::object();
  EXPECT_THAT(write(*binding, &arrived, no_model),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  // Without a pattern there is no path to render.
  EXPECT_THAT(write(*parseOk(R"({"body_mutations": [{"remove": "a"}]})"), nullptr, body),
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
    EXPECT_THAT(writeUnresolved(binding, from, body, unresolved),
                HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr(message)));
    EXPECT_TRUE(unresolved) << message;
  };
  const LlmBindingConstSharedPtr vertex = parseOk(VertexGemini);
  expect_unresolved(*vertex, nullptr, "`{version}`");
  expect_unresolved(*parseOk(absl::StrCat("/v1/", std::string(1, '*'), "/{model}")), &arrived,
                    "wildcard");
  expect_unresolved(
      *parseOk(
          R"({"body_mutations": [{"append": {"record": {"key": "labels", "value": "{tenant}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})"),
      &arrived, "`{tenant}`");

  nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
  bool unresolved = true;
  EXPECT_THAT(writeUnresolved(*vertex, &arrived, body, unresolved), IsOk());
  EXPECT_FALSE(unresolved);
}

TEST(LlmBindingWriteTest, AppliesTheMutationsInOrder) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"body_mutations": [{"remove": "model"}, {"append": {"record": {"key": "max_tokens", "value": 4096}, "action": "ADD_IF_ABSENT"}}, {"append": {"record": {"key": "top_k", "value": 40}, "action": "ADD_IF_ABSENT"}}, {"append": {"record": {"key": "temperature", "value": 0.5}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"record": {"key": "labels", "value": {"tenant": "{project}"}}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"record": {"key": "user", "value": "{project}-{model}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
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
      R"({"body_mutations": [{"append": {"record": {"key": "x", "value": "{tenant}"}, "action": "ADD_IF_ABSENT"}}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"x": "existing"})");
  const nlohmann::json original = body;
  ASSERT_THAT(write(*binding, nullptr, body), IsOkAndHolds(std::nullopt));
  EXPECT_EQ(body, original);
}

// A placeholder fills a JSON string, so any value the request has will do.
TEST(LlmBindingWriteTest, FillsAPlaceholderWithAnyValueTheRequestHas) {
  nlohmann::json body = nlohmann::json::parse(R"({"model": "ibm/granite-3-8b-instruct"})");
  ASSERT_THAT(
      write(
          *parseOk(
              R"({"body_mutations": [{"append": {"record": {"key": "model_id", "value": "{model}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})"),
          nullptr, body),
      IsOk());
  EXPECT_EQ(body["model_id"], "ibm/granite-3-8b-instruct");
  EXPECT_THAT(
      write(
          *parseOk(
              R"({"body_mutations": [{"append": {"record": {"key": "x", "value": "{tenant}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})"),
          nullptr, body),
      HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("`{tenant}`")));
  // An unclosed brace is text.
  ASSERT_THAT(
      write(
          *parseOk(
              R"({"body_mutations": [{"append": {"record": {"key": "x", "value": "a{b"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})"),
          nullptr, body),
      IsOk());
  EXPECT_EQ(body["x"], "a{b");
}

// Leaving in the binding it arrived in, a request gets back the fields the binding writes as the
// client sent them; one the client left out takes the binding's value.
TEST(LlmBindingWriteTest, TheSameBindingRestoresWhatTheClientSent) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({
    "uri_pattern": "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}",
    "body_mutations": [{"remove": "model"}, {"append": {"record": {"key": "anthropic_version", "value": "vertex-2023-10-16"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"record": {"key": "service_tier", "value": "auto"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]
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
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"body_mutations": [{"append": {"record": {"key": "tenant", "value": "{project}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m", "tenant": "client-tenant"})");
  const nlohmann::json original = body;
  const std::optional<LlmBindingRead> arrived = read(binding, "/v1/messages", body);
  ASSERT_TRUE(arrived.has_value());
  EXPECT_FALSE(body.contains("tenant"));
  ASSERT_THAT(write(*binding, &*arrived, body), IsOkAndHolds(std::nullopt));
  EXPECT_EQ(body, original);
}

TEST(LlmBindingWriteTest, EditsOnlyAnObjectBody) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"body_mutations": [{"append": {"record": {"key": "a", "value": 1}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  nlohmann::json array = nlohmann::json::array();
  ASSERT_THAT(write(*binding, nullptr, array), IsOk());
  EXPECT_EQ(array, nlohmann::json::array());
}

// The Anthropic API's own binding: a header beside the body, which the engine hands back to the
// caller rather than writing itself.
constexpr absl::string_view AnthropicApi = R"({
  "uri_pattern": "/v1/messages",
  "header_mutations": [{"append": {"header": {"key": "anthropic-version", "value": "2023-06-01"}, "append_action": "ADD_IF_ABSENT"}}]
})";

TEST(LlmBindingHeaderTest, WritesTheHeadersBesideTheBody) {
  const LlmBindingConstSharedPtr binding = parseOk(R"({
    "uri_pattern": "/gw/{model}",
    "body_mutations": [{"append": {"record": {"key": "tenant", "value": "{project}"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}], "header_mutations": [{"append": {"header": {"key": "anthropic-version", "value": "2023-06-01"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"header": {"key": "x-model", "value": "{model}-{project}"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"remove": "x-client-key"}]
  })");
  const LlmBindingRead arrived{nullptr, "", {{"project", "acme"}}, {}, {}};
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*binding, &arrived, body, nullptr, &headers), IsOkAndHolds("/gw/m"));
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2023-06-01",
                                                        "x-model: m-acme", "x-client-key"));
  EXPECT_EQ(body, nlohmann::json::parse(R"({"model": "m", "tenant": "acme"})"));
}

// `keep_existing` on a header is decided against the request's headers, which the caller lends.
TEST(LlmBindingHeaderTest, KeepsAHeaderTheRequestHas) {
  const LlmBindingConstSharedPtr binding = parseOk(AnthropicApi);
  nlohmann::json body = nlohmann::json::object();
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*binding, nullptr, body, nullptr, &headers, {{"anthropic-version", "2024"}}),
              IsOkAndHolds("/v1/messages"));
  EXPECT_TRUE(headers.empty());
  ASSERT_THAT(write(*binding, nullptr, body, nullptr, &headers), IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2023-06-01"));
}

// `keep_existing` on a header sees the edits before it, as it does on a body field: a header an
// earlier edit set is present, one an earlier edit removed is absent, whatever the request had.
TEST(LlmBindingHeaderTest, KeepExistingSeesEarlierHeaderEdits) {
  // Absent in the request; set a, then set b only if absent: a stands.
  const LlmBindingConstSharedPtr set_then_keep = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "a"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"header": {"key": "x-a", "value": "b"}, "append_action": "ADD_IF_ABSENT"}}]})");
  nlohmann::json body = nlohmann::json::object();
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*set_then_keep, nullptr, body, nullptr, &headers), IsOk());
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("x-a: a"));

  // Present in the request; remove, then set b only if absent: b is written.
  const LlmBindingConstSharedPtr remove_then_keep = parseOk(
      R"({"header_mutations": [{"remove": "x-a"}, {"append": {"header": {"key": "x-a", "value": "b"}, "append_action": "ADD_IF_ABSENT"}}]})");
  headers.clear();
  ASSERT_THAT(write(*remove_then_keep, nullptr, body, nullptr, &headers, {{"x-a", "original"}}),
              IsOk());
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("x-a", "x-a: b"));

  // The same two edits on a body field, for comparison.
  const LlmBindingConstSharedPtr body_edits = parseOk(
      R"({"body_mutations": [{"append": {"record": {"key": "f", "value": "a"}, "action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"record": {"key": "f", "value": "b"}, "action": "ADD_IF_ABSENT"}}]})");
  ASSERT_THAT(write(*body_edits, nullptr, body), IsOk());
  EXPECT_EQ(body, nlohmann::json::parse(R"({"f": "a"})"));
}

// A binding's header edit stands in for the API's edit to the same header.
TEST(LlmBindingHeaderTest, ABindingOverridesAnApiHeaderEditByNamingIt) {
  const LlmBindingConstSharedPtr anthropic = parseOk(AnthropicApi);
  const LlmBindingConstSharedPtr newer = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "anthropic-version", "value": "2024-01-01"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  nlohmann::json body = nlohmann::json::object();
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*newer, nullptr, body, anthropic.get(), &headers),
              IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2024-01-01"));
}

// A header the arrived binding writes is the provider's: recorded on the way in for the caller to
// take out, and given back only when the request leaves in the same binding.
TEST(LlmBindingHeaderTest, TheSameBindingRestoresTheClientsHeader) {
  const LlmBindingConstSharedPtr anthropic = parseOk(AnthropicApi);
  nlohmann::json body = nlohmann::json::parse(R"({"model": "m"})");
  const std::optional<LlmBindingRead> arrived =
      read(anthropic, "/v1/messages", body, nullptr, {{"anthropic-version", "2024-01-01"}});
  ASSERT_TRUE(arrived.has_value());
  EXPECT_THAT(arrived->written_headers,
              ::testing::ElementsAre(::testing::Pair("anthropic-version", "2024-01-01")));
  EXPECT_EQ(body["model"], "m");

  // Back out in the same binding, after the caller took the header out: the client's value.
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*anthropic, &*arrived, body, nullptr, &headers), IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2024-01-01"));

  // Out in another binding that writes the same header: that binding's value.
  const LlmBindingConstSharedPtr gateway = parseOk(
      R"({"uri_pattern": "/gw", "header_mutations": [{"append": {"header": {"key": "anthropic-version", "value": "gw"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  headers.clear();
  ASSERT_THAT(write(*gateway, &*arrived, body, nullptr, &headers), IsOkAndHolds("/gw"));
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: gw"));

  // A client that sent no such header has nothing recorded, and gets the binding's default.
  body = nlohmann::json::parse(R"({"model": "m"})");
  const std::optional<LlmBindingRead> bare = read(anthropic, "/v1/messages", body);
  ASSERT_TRUE(bare.has_value());
  EXPECT_TRUE(bare->written_headers.empty());
  headers.clear();
  ASSERT_THAT(write(*anthropic, &*bare, body, nullptr, &headers), IsOk());
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2023-06-01"));
}

// A header edit is handed back whatever the body is; a failure hands back none.
TEST(LlmBindingHeaderTest, HeadersDoNotDependOnTheBody) {
  const LlmBindingConstSharedPtr binding = parseOk(AnthropicApi);
  nlohmann::json array = nlohmann::json::array();
  std::vector<HeaderMutation> headers;
  ASSERT_THAT(write(*binding, nullptr, array, nullptr, &headers), IsOk());
  EXPECT_THAT(describe(headers), ::testing::ElementsAre("anthropic-version: 2023-06-01"));

  const LlmBindingConstSharedPtr needs_project = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "a"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"header": {"key": "x-tenant", "value": "{project}"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  nlohmann::json body = nlohmann::json::object();
  headers.clear();
  EXPECT_THAT(write(*needs_project, nullptr, body, nullptr, &headers),
              HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(headers.empty());
}

// A value is checked once its placeholders are filled, since a placeholder takes what the client
// sent: a CR, LF or NUL in the result would end the header and start another.
TEST(LlmBindingHeaderTest, RejectsAValueThatWouldSplitTheHeader) {
  const LlmBindingConstSharedPtr binding = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "a"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}, {"append": {"header": {"key": "x-model", "value": "{model}"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  std::vector<HeaderMutation> headers;
  for (absl::string_view model : {absl::string_view("m\r\nx-admin: true"), absl::string_view("m\n"),
                                  absl::string_view("m\0", 2)}) {
    nlohmann::json body;
    body["model"] = std::string(model);
    headers.clear();
    EXPECT_THAT(write(*binding, nullptr, body, nullptr, &headers),
                HasStatusCode(absl::StatusCode::kInvalidArgument));
    EXPECT_TRUE(headers.empty());
  }

  // Set in the binding itself, from filter state, the value is caught the same way.
  const LlmBindingConstSharedPtr injected = parseOk(
      R"({"header_mutations": [{"append": {"header": {"key": "x-a", "value": "a\r\nx-admin: true"}, "append_action": "OVERWRITE_IF_EXISTS_OR_ADD"}}]})");
  nlohmann::json body = nlohmann::json::object();
  EXPECT_THAT(write(*injected, nullptr, body, nullptr, &headers),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
