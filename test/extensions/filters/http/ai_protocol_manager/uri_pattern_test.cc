#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include "test/test_common/status_utility.h"

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

TEST(UriPatternParseTest, RejectsMalformedTemplates) {
  for (absl::string_view pattern : {"", "v1/{model}", "/v1/{model", "/v1/{mo{del}}",
                                    "/{model}/{model}", "/v1//x", " /v1", "/v1 ", "/v1  /v2"}) {
    EXPECT_THAT(UriPattern::parse(pattern), HasStatusCode(absl::StatusCode::kInvalidArgument))
        << pattern;
  }
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
  EXPECT_FALSE(m->stream);

  m = gemini.match("/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.0-flash");
  EXPECT_TRUE(m->stream);
  // The query is ignored on both sides.
  EXPECT_TRUE(gemini.match("/v1beta/models/gemini-2.0-flash:streamGenerateContent")->stream);
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash:generateContent?key=x")->stream);
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
  EXPECT_FALSE(m->stream);
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
    EXPECT_TRUE(m->stream);
  }
  EXPECT_THAT(gemini.render("gemini-2.0-flash", true),
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
  EXPECT_FALSE(gemini.match("/v1/chat/completions").has_value());
}

TEST(UriPatternMatchTest, ChoiceThatExtendsTheOtherIsMatchedWhole) {
  const UriPattern bedrock = parseOk(kBedrock);
  std::optional<UriPattern::Match> m = bedrock.match("/model/anthropic.claude-3/converse-stream");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "anthropic.claude-3");
  EXPECT_TRUE(m->stream);
  m = bedrock.match("/model/anthropic.claude-3/converse");
  ASSERT_TRUE(m.has_value());
  EXPECT_FALSE(m->stream);
}

// Spellings that differ only in their query match alike, and the body says whether to stream.
TEST(UriPatternMatchTest, ChoiceOfQueriesMatchesAsUnary) {
  const UriPattern chat = parseOk("/v1/chat/completions{|?stream=true}");
  for (absl::string_view path : {"/v1/chat/completions", "/v1/chat/completions?stream=true"}) {
    std::optional<UriPattern::Match> m = chat.match(path);
    ASSERT_TRUE(m.has_value()) << path;
    EXPECT_FALSE(m->stream) << path;
  }
}

TEST(UriPatternMatchTest, FixedPatternMatchesOnlyItself) {
  const UriPattern messages = parseOk("/v1/messages");
  std::optional<UriPattern::Match> m = messages.match("/v1/messages?beta=true");
  ASSERT_TRUE(m.has_value());
  EXPECT_TRUE(m->model.empty());
  EXPECT_FALSE(m->stream);
  EXPECT_FALSE(messages.match("/anthropic/v1/messages").has_value());
  EXPECT_FALSE(messages.match("/v1/messages/count_tokens").has_value());
}

TEST(UriPatternRenderTest, RendersModelAndChoice) {
  const UriPattern gemini = parseOk(kGemini);
  EXPECT_THAT(gemini.render("gemini-2.0-flash", false),
              IsOkAndHolds("/v1beta/models/gemini-2.0-flash:generateContent"));
  EXPECT_THAT(gemini.render("gemini-2.0-flash", true),
              IsOkAndHolds("/v1beta/models/gemini-2.0-flash:streamGenerateContent?alt=sse"));
  EXPECT_THAT(parseOk(kVertexClaude).render("claude-sonnet-4@20250514", true),
              IsOkAndHolds("/v1/projects/p/locations/l/publishers/anthropic/models/"
                           "claude-sonnet-4@20250514:streamRawPredict"));
  EXPECT_THAT(parseOk(kBedrock).render("anthropic.claude-3", false),
              IsOkAndHolds("/model/anthropic.claude-3/converse"));
  EXPECT_THAT(parseOk("/v1/chat/completions{|?stream=true}").render("", true),
              IsOkAndHolds("/v1/chat/completions?stream=true"));
}

TEST(UriPatternRenderTest, FixedPatternIgnoresTheModel) {
  EXPECT_THAT(parseOk("/v1/messages").render("", false), IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(parseOk("/v1/messages").render("../x", true), IsOkAndHolds("/v1/messages"));
}

// Variables other than `{model}`, and wildcards, stand for segments only a matched path knows.
TEST(UriPatternRenderTest, RefusesSegmentsTheModelDoesNotFill) {
  for (absl::string_view pattern : std::initializer_list<absl::string_view>{
           kVertexGemini, "/v1/{model=*}:predict", "/v1/{model}/{rest=**}"}) {
    EXPECT_THAT(parseOk(pattern).render("gemini-2.0-flash", false),
                HasStatus(absl::StatusCode::kInvalidArgument,
                          HasSubstr("has segments other than `{model}` to fill")))
        << pattern;
  }
}

// The model becomes a path segment, so anything that could escape one is refused.
TEST(UriPatternRenderTest, RefusesAModelThatIsNotAnId) {
  const UriPattern gemini = parseOk(kGemini);
  for (absl::string_view bad : {"", "../admin", "a/b", "a?b", "a#b", "a%2Fb", "a b", "a:b"}) {
    EXPECT_THAT(gemini.render(bad, false), HasStatusCode(absl::StatusCode::kInvalidArgument))
        << bad;
  }
}

TEST(UriPatternRenderTest, RoundTrips) {
  for (absl::string_view pattern : {kGemini, kVertexClaude, kBedrock}) {
    const UriPattern parsed = parseOk(pattern);
    for (const bool stream : {false, true}) {
      const absl::StatusOr<std::string> rendered = parsed.render("m-1.5", stream);
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
