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
constexpr absl::string_view kVertexClaude =
    "/v1/projects/p/locations/l/publishers/anthropic/models/{model}:{rawPredict|streamRawPredict}";
constexpr absl::string_view kBedrock = "/model/{model}/{converse|converse-stream}";

UriPattern parseOk(absl::string_view pattern) {
  absl::StatusOr<UriPattern> parsed = UriPattern::parse(pattern);
  EXPECT_THAT(parsed, IsOk()) << pattern;
  return *parsed;
}

TEST(UriPatternParseTest, RejectsMalformedPatterns) {
  EXPECT_THAT(UriPattern::parse(""), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(UriPattern::parse("/v1/{model"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(UriPattern::parse("/v1/{mo{del}}"),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(UriPattern::parse("/v1/{project}/x"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("unknown placeholder")));
  EXPECT_THAT(UriPattern::parse("/{model}/{model}"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("more than one")));
  EXPECT_THAT(UriPattern::parse("/{a|b}/{c|d}"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("more than one")));
  EXPECT_THAT(UriPattern::parse("/{a|b|c}"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("exactly two")));
  // The model must end at a literal.
  EXPECT_THAT(UriPattern::parse("/models/{model}{a|b}"),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("followed by a literal")));
}

TEST(UriPatternParseTest, AcceptsTheShapesInUse) {
  EXPECT_TRUE(parseOk(kGemini).hasModel());
  EXPECT_TRUE(parseOk(kVertexClaude).hasModel());
  EXPECT_TRUE(parseOk(kBedrock).hasModel());
  EXPECT_FALSE(parseOk("/v1/messages").hasModel());
  // A model at the end, and empty alternatives, are allowed.
  EXPECT_TRUE(parseOk("/models/{model}").hasModel());
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

// A prefix in front of the API's own path, such as Vertex AI's, does not hide it.
TEST(UriPatternMatchTest, LeadingLiteralAnchorsOnItsLastSegment) {
  const UriPattern gemini = parseOk(kGemini);
  std::optional<UriPattern::Match> m = gemini.match(
      "/v1/projects/p/locations/l/publishers/google/models/gemini-2.0-flash:generateContent");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "gemini-2.0-flash");
  // But the last segment itself must be there.
  EXPECT_FALSE(gemini.match("/v1beta/gemini-2.0-flash:generateContent").has_value());
}

TEST(UriPatternMatchTest, RejectsWhatThePatternDoesNotName) {
  const UriPattern gemini = parseOk(kGemini);
  EXPECT_FALSE(gemini.match("/v1beta/models/:generateContent").has_value()); // empty model
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash").has_value()); // no method
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash:countTokens").has_value());
  EXPECT_FALSE(gemini.match("/v1beta/models/gemini-2.0-flash:generateContentX").has_value());
  EXPECT_FALSE(gemini.match("/v1beta/models/a/b:generateContent").has_value()); // slash in model
  EXPECT_FALSE(gemini.match("/v1/chat/completions").has_value());
}

TEST(UriPatternMatchTest, AlternativeThatExtendsTheOtherIsMatchedWhole) {
  const UriPattern bedrock = parseOk(kBedrock);
  std::optional<UriPattern::Match> m = bedrock.match("/model/anthropic.claude-3/converse-stream");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->model, "anthropic.claude-3");
  EXPECT_TRUE(m->stream);
  m = bedrock.match("/model/anthropic.claude-3/converse");
  ASSERT_TRUE(m.has_value());
  EXPECT_FALSE(m->stream);
}

TEST(UriPatternMatchTest, FixedPatternMatchesOnlyItself) {
  const UriPattern messages = parseOk("/v1/messages");
  std::optional<UriPattern::Match> m = messages.match("/v1/messages?beta=true");
  ASSERT_TRUE(m.has_value());
  EXPECT_TRUE(m->model.empty());
  EXPECT_FALSE(m->stream);
  EXPECT_TRUE(messages.match("/anthropic/v1/messages").has_value()); // gateway prefix
  EXPECT_FALSE(messages.match("/v1/messages/count_tokens").has_value());
}

TEST(UriPatternRenderTest, RendersModelAndAlternative) {
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
}

TEST(UriPatternRenderTest, FixedPatternIgnoresTheModel) {
  EXPECT_THAT(parseOk("/v1/messages").render("", false), IsOkAndHolds("/v1/messages"));
  EXPECT_THAT(parseOk("/v1/messages").render("../x", true), IsOkAndHolds("/v1/messages"));
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
