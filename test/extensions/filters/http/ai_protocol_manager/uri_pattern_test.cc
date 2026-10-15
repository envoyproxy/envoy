#include <optional>
#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include "test/test_common/status_utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

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

constexpr absl::string_view kGemini =
    "/v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}";
constexpr absl::string_view kVertexGemini =
    "/{version}/projects/{project}/locations/{location}/publishers/google/models/"
    "{model}:{generateContent|streamGenerateContent?alt=sse}";
constexpr absl::string_view kVertexClaude =
    "/v1/projects/p/locations/l/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}";
constexpr absl::string_view kBedrock = "/model/{model}/{converse|converse-stream}";

UriPattern parseOk(absl::string_view pattern) {
  absl::StatusOr<UriPattern> parsed = UriPattern::parse(pattern);
  EXPECT_THAT(parsed, IsOk()) << pattern;
  return *parsed;
}

using Values = absl::flat_hash_map<std::string, std::string>;

// An unnamed wildcard segment, kept out of literals the spelling check reads as comments.
constexpr absl::string_view Wildcard = "*";

absl::StatusOr<std::string> render(const UriPattern& pattern, bool stream, const Values& values) {
  return pattern.render(stream, [&values](absl::string_view name) -> std::optional<std::string> {
    const auto value = values.find(name);
    if (value == values.end()) {
      return std::nullopt;
    }
    return value->second;
  });
}

TEST(IsPathTokenTest, AcceptsOnlyWhatStaysInOneSegment) {
  for (absl::string_view token :
       {"gemini-2.5-flash", "claude-sonnet-4-5@20250929", "v1", "us-east5", "my_project.1", "0"}) {
    EXPECT_TRUE(isPathToken(token)) << token;
  }
  for (absl::string_view token :
       {"", ".", "..", ".hidden", "-x", "@x", "a/b", "a?b", "a#b", "a%2Fb", "a b", "a:b"}) {
    EXPECT_FALSE(isPathToken(token)) << token;
  }
}

TEST(UriPatternParseTest, RejectsMalformedTemplates) {
  for (absl::string_view pattern : {"", "v1/{model}", "/v1/{model", "/v1/{mo{del}}",
                                    "/{model}/{model}", "/v1//x", " /v1", "/v1 ", "/v1  /v2"}) {
    EXPECT_THAT(UriPattern::parse(pattern), HasStatusCode(absl::StatusCode::kInvalidArgument))
        << pattern;
  }
}

// A named variable's value must be one path token, so a variable spanning segments could only
// ever match one.
TEST(UriPatternParseTest, RejectsNamedVariablesOverSeveralSegments) {
  for (const std::string& pattern : {std::string("/v1/{model}/{rest=**}"),
                                     absl::StrCat("/v1/{name=models/", Wildcard, "}:predict")}) {
    EXPECT_THAT(UriPattern::parse(pattern),
                HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("must match one path")))
        << pattern;
  }
  EXPECT_TRUE(parseOk("/v1/{model=*}:predict").hasModel());
}

// Rendering cannot fill an unnamed wildcard in the layout it writes.
TEST(UriPatternParseTest, KnowsWhetherItCanRenderItsLayout) {
  EXPECT_TRUE(parseOk(kVertexGemini).renderable());
  EXPECT_FALSE(parseOk(absl::StrCat("/v1/", Wildcard, "/models/{model}")).renderable());
  EXPECT_TRUE(parseOk(absl::StrCat("/v1/models/{model} /gw/", Wildcard, "/{model}")).renderable());
}

TEST(UriPatternParseTest, RejectsAnythingButOneChoiceOfTwo) {
  for (absl::string_view pattern : {"/{a|b}/{c|d}", "/{a|b|c}", "/{a|b", "/a|b", "/{x{model}|y}"}) {
    EXPECT_THAT(UriPattern::parse(pattern),
                HasStatus(absl::StatusCode::kInvalidArgument,
                          HasSubstr("expected at most one `{unary|stream}`")))
        << pattern;
  }
}

TEST(UriPatternParseTest, AcceptsTheShapesInUse) {
  EXPECT_TRUE(parseOk(kGemini).hasModel());
  EXPECT_TRUE(parseOk(kVertexGemini).hasModel());
  EXPECT_TRUE(parseOk(kVertexClaude).hasModel());
  EXPECT_TRUE(parseOk(kBedrock).hasModel());
  EXPECT_FALSE(parseOk("/v1/messages").hasModel());
  EXPECT_FALSE(parseOk("/v1/{project}/messages").hasModel());
  // A model at the end, a model followed by a choice, and an empty choice are allowed.
  EXPECT_TRUE(parseOk("/models/{model}").hasModel());
  EXPECT_TRUE(parseOk("/models/{model}{:unary|:stream}").hasModel());
  EXPECT_FALSE(parseOk("/v1/chat/completions{|?stream=true}").hasModel());
  EXPECT_EQ(parseOk(kGemini).source(), kGemini);
}

TEST(UriPatternMatchTest, LiftsModelAndStreamMode) {
  const UriPattern gemini = parseOk(kGemini);
  std::optional<UriPattern::Match> m =
      gemini.match("/v1beta/models/gemini-2.0-flash:generateContent");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.0-flash");
  EXPECT_EQ(m->stream, false);

  m = gemini.match("/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.0-flash");
  EXPECT_EQ(m->stream, true);
  // The query is ignored on both sides.
  EXPECT_EQ(gemini.match("/v1beta/models/gemini-2.0-flash:streamGenerateContent")->stream, true);
  EXPECT_EQ(gemini.match("/v1beta/models/gemini-2.0-flash:generateContent?key=x")->stream, false);
}

// A prefix in front of the API's own path is part of the path: a pattern that accepts one says
// so, with a variable or a wildcard.
TEST(UriPatternMatchTest, MatchesTheWholePath) {
  const absl::string_view vertex =
      "/v1/projects/p/locations/l/publishers/google/models/gemini-2.0-flash:generateContent";
  EXPECT_FALSE(parseOk(kGemini).match(vertex).has_value());
  EXPECT_FALSE(
      parseOk(kGemini).match("/gw/v1beta/models/gemini-2.0-flash:generateContent").has_value());

  std::optional<UriPattern::Match> m = parseOk(kVertexGemini).match(vertex);
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.0-flash");
  EXPECT_EQ(m->stream, false);
}

// Every named variable reads a value; `{model}` has a field of its own.
TEST(UriPatternMatchTest, ReadsEveryNamedVariable) {
  std::optional<UriPattern::Match> m =
      parseOk(kVertexGemini)
          .match("/v1/projects/acme/locations/us-east5/publishers/google/models/gemini-2.5-flash:"
                 "streamGenerateContent?alt=sse");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.5-flash");
  EXPECT_EQ(m->stream, true);
  EXPECT_EQ(m->variables,
            (Values{{"version", "v1"}, {"project", "acme"}, {"location", "us-east5"}}));

  // Unnamed wildcards and the query read nothing.
  m = parseOk(absl::StrCat("/gw/", Wildcard, "/{tenant}/v1/chat/completions"))
          .match("/gw/x/t-1/v1/chat/completions?key=k");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->variables, (Values{{"tenant", "t-1"}}));
}

// A path matches the first layout that fits it, and the first layout is the one rendered.
TEST(UriPatternMatchTest, MatchesAnyLayout) {
  const UriPattern gemini = parseOk(std::string(kGemini) + " " + std::string(kVertexGemini));
  for (absl::string_view path : {"/v1beta/models/gemini-2.0-flash:streamGenerateContent",
                                 "/v1/projects/p/locations/l/publishers/google/models/"
                                 "gemini-2.0-flash:streamGenerateContent"}) {
    std::optional<UriPattern::Match> m = gemini.match(path);
    ASSERT_TRUE(m.has_value()) << path;
    EXPECT_EQ(m->model, "gemini-2.0-flash");
    EXPECT_EQ(m->stream, true);
  }
  EXPECT_THAT(render(gemini, true, {{"model", "gemini-2.0-flash"}}),
              IsOkAndHolds("/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse"));
}

// Layouts need not agree: a path names the model only when its own layout does.
TEST(UriPatternMatchTest, LayoutsNameTheModelOnTheirOwn) {
  const UriPattern chat =
      parseOk("/v1/chat/completions /openai/deployments/{model}/chat/completions");
  EXPECT_FALSE(chat.hasModel());
  EXPECT_EQ(chat.match("/v1/chat/completions")->model, "");
  EXPECT_EQ(chat.match("/openai/deployments/gpt-4o/chat/completions?api-version=1")->model,
            "gpt-4o");
}

TEST(UriPatternMatchTest, RejectsWhatThePatternDoesNotName) {
  const UriPattern gemini = parseOk(kGemini);
  EXPECT_FALSE(gemini.match("/v1beta/models/:generateContent").has_value()); // empty model
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash").has_value()); // no method
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash:countTokens").has_value());
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash:generateContentX").has_value());
  EXPECT_FALSE(gemini.match("/v1beta/models/a/b:generateContent").has_value()); // slash in model
  EXPECT_FALSE(gemini.match("/v1beta/models/a:b:generateContent").has_value()); // not a model id
  EXPECT_FALSE(gemini.match("/v1beta/models/a%2Fb:generateContent").has_value());
  EXPECT_FALSE(gemini.match("/v1beta/models/..:generateContent").has_value());
  EXPECT_FALSE(gemini.match("/v1/chat/completions").has_value());
  // Every variable's value must be a path token, not just the model's.
  EXPECT_FALSE(parseOk(kVertexGemini)
                   .match("/v1/projects/../locations/l/publishers/google/models/m:generateContent")
                   .has_value());
}

TEST(UriPatternMatchTest, ChoiceThatExtendsTheOtherIsMatchedWhole) {
  const UriPattern bedrock = parseOk(kBedrock);
  std::optional<UriPattern::Match> m = bedrock.match("/model/anthropic.claude-3/converse-stream");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "anthropic.claude-3");
  EXPECT_EQ(m->stream, true);
  m = bedrock.match("/model/anthropic.claude-3/converse");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->stream, false);
}

// Spellings that differ only in their query match alike, and the body says whether to stream.
TEST(UriPatternMatchTest, ChoiceOfQueriesLeavesTheModeToTheBody) {
  const UriPattern chat = parseOk("/v1/chat/completions{|?stream=true}");
  for (absl::string_view path : {"/v1/chat/completions", "/v1/chat/completions?stream=true"}) {
    std::optional<UriPattern::Match> m = chat.match(path);
    ASSERT_TRUE(m.has_value()) << path;
    EXPECT_EQ(m->stream, std::nullopt) << path;
  }
}

TEST(UriPatternMatchTest, FixedPatternMatchesOnlyItself) {
  const UriPattern messages = parseOk("/v1/messages");
  std::optional<UriPattern::Match> m = messages.match("/v1/messages?beta=true");
  ASSERT_TRUE(m.has_value());
  EXPECT_TRUE(m->model.empty());
  EXPECT_EQ(m->stream, std::nullopt);
  EXPECT_FALSE(messages.match("/anthropic/v1/messages").has_value());
  EXPECT_FALSE(messages.match("/v1/messages/count_tokens").has_value());
}

TEST(UriPatternRenderTest, RendersModelAndChoice) {
  const UriPattern gemini = parseOk(kGemini);
  const Values model{{"model", "gemini-2.0-flash"}};
  EXPECT_THAT(render(gemini, false, model),
              IsOkAndHolds("/v1beta/models/gemini-2.0-flash:generateContent"));
  EXPECT_THAT(render(gemini, true, model),
              IsOkAndHolds("/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse"));
  EXPECT_THAT(render(parseOk(kVertexClaude), true, {{"model", "claude-sonnet-4@20250514"}}),
              IsOkAndHolds("/v1/projects/p/locations/l/publishers/anthropic/models/"
                           "claude-sonnet-4@20250514:streamRawPredict"));
  EXPECT_THAT(render(parseOk(kBedrock), false, {{"model", "anthropic.claude-3"}}),
              IsOkAndHolds("/model/anthropic.claude-3/converse"));
  EXPECT_THAT(render(parseOk("/v1/chat/completions{|?stream=true}"), true, {}),
              IsOkAndHolds("/v1/chat/completions?stream=true"));
}

TEST(UriPatternRenderTest, FixedPatternReadsNoValues) {
  EXPECT_THAT(render(parseOk("/v1/messages"), false, {}), IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(render(parseOk("/v1/messages"), true, {{"model", "../x"}}),
              IsOkAndHolds("/v1/messages"));
}

TEST(UriPatternRenderTest, RendersEveryNamedVariable) {
  EXPECT_THAT(render(parseOk(kVertexGemini), true,
                     {{"version", "v1"},
                      {"project", "acme"},
                      {"location", "us-east5"},
                      {"model", "gemini-2.5-flash"}}),
              IsOkAndHolds("/v1/projects/acme/locations/us-east5/publishers/google/models/"
                           "gemini-2.5-flash:streamGenerateContent?alt=sse"));
  EXPECT_THAT(render(parseOk("/v1/{model=*}:predict"), false, {{"model", "m"}}),
              IsOkAndHolds("/v1/m:predict"));
}

// A variable without a value, or an unnamed wildcard, stands for a segment only a matched path
// knows.
TEST(UriPatternRenderTest, RefusesSegmentsWithoutAValue) {
  EXPECT_THAT(
      render(parseOk(kVertexGemini), false, {{"model", "gemini-2.0-flash"}}),
      HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("has no value for `{version}`")));
  EXPECT_THAT(
      render(parseOk(absl::StrCat("/v1/", Wildcard, "/models/{model}")), false, {{"model", "m"}}),
      HasStatus(absl::StatusCode::kFailedPrecondition, HasSubstr("wildcard")));
}

// Every value becomes a path segment, so anything that could escape one is refused.
TEST(UriPatternRenderTest, RefusesAValueThatIsNotAPathToken) {
  const UriPattern gemini = parseOk(kGemini);
  for (absl::string_view bad : {"", "../admin", "a/b", "a?b", "a#b", "a%2Fb", "a b", "a:b"}) {
    EXPECT_THAT(render(gemini, false, {{"model", std::string(bad)}}),
                HasStatusCode(absl::StatusCode::kInvalidArgument))
        << bad;
  }
  EXPECT_THAT(render(parseOk(kVertexGemini), false,
                     {{"version", "v1"}, {"project", ".."}, {"location", "l"}, {"model", "m"}}),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(UriPatternRenderTest, RoundTrips) {
  for (absl::string_view pattern : {kGemini, kVertexClaude, kBedrock}) {
    const UriPattern parsed = parseOk(pattern);
    for (const bool stream : {false, true}) {
      const absl::StatusOr<std::string> rendered = render(parsed, stream, {{"model", "m-1.5"}});
      ASSERT_THAT(rendered, IsOk());
      const std::optional<UriPattern::Match> m = parsed.match(*rendered);
      ASSERT_TRUE(m.has_value()) << *rendered;
      EXPECT_EQ(m->model, "m-1.5");
      EXPECT_EQ(m->stream, stream);
    }
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
