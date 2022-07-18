#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/pattern_template/pattern_template_matching_internal.h"

#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

namespace Envoy {
namespace PatternTemplate {

namespace PatternTemplateInternal {

namespace {

using ::Envoy::StatusHelpers::StatusIs;

TEST(InternalParsing, ParsedUrlDebugString) {
  ParsedUrlPattern patt1 = {
      {
          "abc",
          "def",
          Operator::kPathGlob,
          Variable("var", {Operator::kPathGlob, "ghi", Operator::kTextGlob}),
      },
      ".test",
      {},
  };
  EXPECT_EQ(patt1.DebugString(), "/abc/def/*/{var=*/ghi/**}.test");

  ParsedUrlPattern patt2 = {{
                                Variable("var", {}),
                            },
                            "",
                            {}};
  EXPECT_EQ(patt2.DebugString(), "/{var}");
}

TEST(InternalParsing, isValidLiteralWorks) {
  EXPECT_TRUE(isValidLiteral("123abcABC"));
  EXPECT_TRUE(isValidLiteral("._~-"));
  EXPECT_TRUE(isValidLiteral("-._~%20!$&'()+,;:@"));
  EXPECT_FALSE(isValidLiteral("`~!@#$%^&()-_+;:,<.>'\"\\| "));
  EXPECT_FALSE(isValidLiteral("abc/"));
  EXPECT_FALSE(isValidLiteral("ab*c"));
  EXPECT_FALSE(isValidLiteral("a**c"));
  EXPECT_FALSE(isValidLiteral("a=c"));
  EXPECT_FALSE(isValidLiteral("?abc"));
  EXPECT_FALSE(isValidLiteral("?a=c"));
  EXPECT_FALSE(isValidLiteral("{abc"));
  EXPECT_FALSE(isValidLiteral("abc}"));
  EXPECT_FALSE(isValidLiteral("{abc}"));
}

TEST(InternalParsing, IsValidRewriteLiteralWorks) {
  EXPECT_TRUE(isValidRewriteLiteral("123abcABC"));
  EXPECT_TRUE(isValidRewriteLiteral("abc/"));
  EXPECT_TRUE(isValidRewriteLiteral("abc/def"));
  EXPECT_TRUE(isValidRewriteLiteral("/abc.def"));
  EXPECT_TRUE(isValidRewriteLiteral("._~-"));
  EXPECT_TRUE(isValidRewriteLiteral("-._~%20!$&'()+,;:@"));
  EXPECT_FALSE(isValidRewriteLiteral("`~!@#$%^&()-_+;:,<.>'\"| "));
  EXPECT_FALSE(isValidRewriteLiteral("ab}c"));
  EXPECT_FALSE(isValidRewriteLiteral("ab{c"));
  EXPECT_FALSE(isValidRewriteLiteral("a=c"));
  EXPECT_FALSE(isValidRewriteLiteral("?a=c"));
}

TEST(InternalParsing, IsValidIndentWorks) {
  EXPECT_TRUE(isValidIndent("abc"));
  EXPECT_TRUE(isValidIndent("ABC_def_123"));
  EXPECT_TRUE(isValidIndent("a1"));
  EXPECT_TRUE(isValidIndent("T"));
  EXPECT_FALSE(isValidIndent("123"));
  EXPECT_FALSE(isValidIndent("__undefined__"));
  EXPECT_FALSE(isValidIndent("abc-def"));
  EXPECT_FALSE(isValidIndent("abc=def"));
  EXPECT_FALSE(isValidIndent("abc def"));
  EXPECT_FALSE(isValidIndent("a!!!"));
}

TEST(InternalParsing, ConsumeLiteralWorks) {
  std::string pattern = "abc/123";

  absl::StatusOr<ParsedResult<Literal>> result = consumeLiteral(pattern);

  ASSERT_OK(result);
  EXPECT_EQ(result->parsed_value, "abc");
  EXPECT_EQ(result->unconsumed_pattern, "/123");
}

TEST(InternalParsing, ConsumeTextGlob) {
  std::string pattern = "***abc/123";

  absl::StatusOr<ParsedResult<Operator>> result = consumeOperator(pattern);

  ASSERT_OK(result);
  EXPECT_EQ(result->parsed_value, Operator::kTextGlob);
  EXPECT_EQ(result->unconsumed_pattern, "*abc/123");
}

TEST(InternalParsing, ConsumePathGlob) {
  std::string pattern = "*/123";

  absl::StatusOr<ParsedResult<Operator>> result = consumeOperator(pattern);

  ASSERT_OK(result);
  EXPECT_EQ(result->parsed_value, Operator::kPathGlob);
  EXPECT_EQ(result->unconsumed_pattern, "/123");
}

class ConsumeVariableSuccess : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ConsumeVariableSuccessTestSuite, ConsumeVariableSuccess,
                         testing::Values("{var=*}", "{Var}", "{v1=**}", "{v_1=*/abc/**}",
                                         "{v3=abc}", "{v=123/*/*}", "{var=abc/*/def}"));

TEST_P(ConsumeVariableSuccess, ConsumeVariableSuccessTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  absl::StatusOr<ParsedResult<Variable>> result = consumeVariable(pattern);

  ASSERT_OK(result);
  EXPECT_EQ(result->parsed_value.DebugString(), pattern);
  EXPECT_TRUE(result->unconsumed_pattern.empty());
}

class ConsumeVariableFailure : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ConsumeVariableFailureTestSuite, ConsumeVariableFailure,
                         testing::Values("{var", "{=abc}", "{_var=*}", "{1v}", "{1v=abc}",
                                         "{var=***}", "{v-a-r}", "{var=*/abc?q=1}", "{var=abc/a*}",
                                         "{var=*def/abc}", "{var=}", "{var=abc=def}",
                                         "{rc=||||(A+yl/}"));

TEST_P(ConsumeVariableFailure, ConsumeVariableFailureTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_THAT(consumeVariable(pattern), StatusIs(absl::StatusCode::kInvalidArgument));
}

class parseURLPatternSyntaxSuccess : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(
    parseURLPatternSyntaxSuccessTestSuite, parseURLPatternSyntaxSuccess,
    testing::Values("/**.m3u8", "/**.mpd", "/*_suf", "/{path=**}.m3u8", "/{foo}/**.ts",
                    "/media/*.m4s", "/media/{contentId=*}/**", "/media/*", "/api/*/*/**",
                    "/api/*/v1/**", "/api/*/v1/*", "/{version=api/*}/*", "/api/*/*/",
                    "/api/*/1234/", "/api/*/{resource=*}/{method=*}",
                    "/api/*/{resource=*}/{method=**}", "/v1/**", "/media/{country}/{lang=*}/**",
                    "/{foo}/{bar}/{fo}/{fum}/*", "/{foo=*}/{bar=*}/{fo=*}/{fum=*}/*",
                    "/media/{id=*}/*", "/media/{contentId=**}",
                    "/api/{version}/projects/{project}/locations/{location}/{resource}/"
                    "{name}",
                    "/api/{version=*}/{url=**}", "/api/{VERSION}/{version}/{verSION}",
                    "/api/1234/abcd", "/media/abcd/%10%20%30/{v1=*/%10%20}_suffix", "/"));

TEST_P(ParseURLPatternSyntaxSuccess, ParseURLPatternSyntaxSuccessTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  absl::StatusOr<ParsedUrlPattern> parsed_patt = parseURLPatternSyntax(pattern);
  ASSERT_OK(parsed_patt);
  EXPECT_EQ(parsed_patt->DebugString(), pattern);
}

class parseURLPatternSyntaxFailure : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(
    ParseURLPatternSyntaxFailureTestSuite, ParseURLPatternSyntaxFailure,
    testing::Values("/api/v*/1234", "/api/{version=v*}/1234", "/api/v{versionNum=*}/1234",
                    "/api/{version=*beta}/1234", "/media/eff456/ll-sd-out.{ext}",
                    "/media/eff456/ll-sd-out.{ext=*}", "/media/eff456/ll-sd-out.**",
                    "/media/{country=**}/{lang=*}/**", "/media/**/*/**", "/link/{id=*}/asset*",
                    "/link/{id=*}/{asset=asset*}", "/media/{id=/*}/*", "/media/{contentId=/**}",
                    "/api/{version}/{version}", "/api/{version.major}/{version.minor}",
                    "/media/***", "/media/*{*}*", "/media/{*}/", "/media/*/index?a=2", "media",
                    "/\001\002\003\004\005\006\007", "/*(/**", "/**/{var}",
                    "/{var1}/{var2}/{var3}/{var4}/{var5}/{var6}", "/{=*}",
                    "/{var12345678901234=*}"));

TEST_P(ParseURLPatternSyntaxFailure, ParseURLPatternSyntaxFailureTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_THAT(parseURLPatternSyntax(pattern), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(InternalRegexGen, LiteralEscapes) {
  EXPECT_EQ(toRegexPattern("abcABC123/-._~%20!$&'()+,;:@"),
            "abcABC123/-\\._~%20!\\$&'\\(\\)\\+,;:@");
}

TEST(InternalRegexGen, LiteralMatches) {
  absl::string_view kPattern = "abcABC123/-._~%20!$&'()+,;:@";

  EXPECT_TRUE(RE2::FullMatch(toStringPiece(kPattern), toRegexPattern(kPattern)));
}

TEST(InternalRegexGen, LiteralMatchesInNamedCapture) {
  absl::string_view kPattern = "abcABC123/-._~%20!$&'()+,;:@";

  RE2 regex = RE2(absl::StrCat("(?P<var>", toRegexPattern(kPattern), ")"));
  ASSERT_EQ(regex.NumberOfCapturingGroups(), 1);

  // Full matched string + capture groups
  std::vector<re2::StringPiece> captures(2);
  ASSERT_TRUE(regex.Match(toStringPiece(kPattern), /*startpos=*/0, /*endpos=*/kPattern.size(),
                          RE2::ANCHOR_BOTH, captures.data(), captures.size()));

  // Index 0 would be the full text of the matched string.
  EXPECT_EQ(toStringPiece(kPattern), captures[0]);
  // Get the pattern matched with the named capture group.
  EXPECT_EQ(toStringPiece(kPattern), captures.at(regex.NamedCapturingGroups().at("var")));
}

TEST(InternalRegexGen, LiteralOnlyMatchesItself) {
  constexpr absl::string_view kChars = "abcABC123/-._~%20!$&'()+,;:@";

  for (const char c : kChars) {
    std::string s = {'z', c, 'z'};
    EXPECT_TRUE(RE2::FullMatch(s, toRegexPattern(s)));
    EXPECT_FALSE(RE2::FullMatch("zzz", toRegexPattern(s)));
  }
}

TEST(InternalRegexGen, RegexLikePatternIsMatchedLiterally) {
  EXPECT_TRUE(RE2::FullMatch("(abc)", toRegexPattern("(abc)")));
  EXPECT_FALSE(RE2::FullMatch("abc", toRegexPattern("(abc)+")));

  EXPECT_TRUE(RE2::FullMatch("(abc)+", toRegexPattern("(abc)+")));
  EXPECT_FALSE(RE2::FullMatch("", toRegexPattern("(abc)+")));
  EXPECT_FALSE(RE2::FullMatch("abc", toRegexPattern("(abc)+")));
  EXPECT_FALSE(RE2::FullMatch("abcabc", toRegexPattern("(abc)+")));

  EXPECT_TRUE(RE2::FullMatch(".+", toRegexPattern(".+")));
  EXPECT_FALSE(RE2::FullMatch("abc", toRegexPattern(".+")));

  EXPECT_TRUE(RE2::FullMatch("a+", toRegexPattern("a+")));
  EXPECT_FALSE(RE2::FullMatch("aa", toRegexPattern("a+")));
}

TEST(InternalRegexGen, DollarSignMatchesIfself) {
  EXPECT_TRUE(RE2::FullMatch("abc$", toRegexPattern("abc$")));
  EXPECT_FALSE(RE2::FullMatch("abc", toRegexPattern("abc$")));
}

TEST(InternalRegexGen, OperatorRegexPattern) {
  EXPECT_EQ(toRegexPattern(Operator::kPathGlob), "[a-zA-Z0-9-._~%!$&'()+,;:@]+");
  EXPECT_EQ(toRegexPattern(Operator::kTextGlob), "[a-zA-Z0-9-._~%!$&'()+,;:@/]*");
}

TEST(InternalRegexGen, PathGlobRegex) {
  EXPECT_TRUE(RE2::FullMatch("abc.123", toRegexPattern(Operator::kPathGlob)));
  EXPECT_FALSE(RE2::FullMatch("", toRegexPattern(Operator::kPathGlob)));
  EXPECT_FALSE(RE2::FullMatch("abc/123", toRegexPattern(Operator::kPathGlob)));
  EXPECT_FALSE(RE2::FullMatch("*", toRegexPattern(Operator::kPathGlob)));
  EXPECT_FALSE(RE2::FullMatch("**", toRegexPattern(Operator::kPathGlob)));
  EXPECT_FALSE(RE2::FullMatch("abc*123", toRegexPattern(Operator::kPathGlob)));
}

TEST(InternalRegexGen, TextGlobRegex) {
  EXPECT_TRUE(RE2::FullMatch("abc.123", toRegexPattern(Operator::kTextGlob)));
  EXPECT_TRUE(RE2::FullMatch("", toRegexPattern(Operator::kTextGlob)));
  EXPECT_TRUE(RE2::FullMatch("abc/123", toRegexPattern(Operator::kTextGlob)));
  EXPECT_FALSE(RE2::FullMatch("*", toRegexPattern(Operator::kTextGlob)));
  EXPECT_FALSE(RE2::FullMatch("**", toRegexPattern(Operator::kTextGlob)));
  EXPECT_FALSE(RE2::FullMatch("abc*123", toRegexPattern(Operator::kTextGlob)));
}

TEST(InternalRegexGen, VariableRegexPattern) {
  EXPECT_EQ(toRegexPattern(Variable("var1", {})), "(?P<var1>[a-zA-Z0-9-._~%!$&'()+,;:@]+)");
  EXPECT_EQ(toRegexPattern(Variable("var2", {Operator::kPathGlob, "abc", Operator::kTextGlob})),
            "(?P<var2>[a-zA-Z0-9-._~%!$&'()+,;:@]+/abc/"
            "[a-zA-Z0-9-._~%!$&'()+,;:@/]*)");
}

TEST(InternalRegexGen, VariableRegexDefaultMatch) {
  absl::StatusOr<ParsedResult<Variable>> var = consumeVariable("{var}");
  ASSERT_OK(var);

  std::string capture;
  EXPECT_TRUE(RE2::FullMatch("abc", toRegexPattern(var->parsed_value), &capture));
  EXPECT_EQ(capture, "abc");
}

TEST(InternalRegexGen, VariableRegexDefaultNotMatch) {
  absl::StatusOr<ParsedResult<Variable>> var = consumeVariable("{var}");
  ASSERT_OK(var);

  EXPECT_FALSE(RE2::FullMatch("abc/def", toRegexPattern(var->parsed_value)));
}

TEST(InternalRegexGen, VariableRegexSegmentsMatch) {
  absl::StatusOr<ParsedResult<Variable>> var = consumeVariable("{var=abc/*/def}");
  ASSERT_OK(var);

  std::string capture;
  EXPECT_TRUE(RE2::FullMatch("abc/123/def", toRegexPattern(var->parsed_value), &capture));
  EXPECT_EQ(capture, "abc/123/def");
}

TEST(InternalRegexGen, VariableRegexTextGlobMatch) {
  absl::StatusOr<ParsedResult<Variable>> var = consumeVariable("{var=**/def}");
  ASSERT_OK(var);

  std::string capture;
  EXPECT_TRUE(RE2::FullMatch("abc/123/def", toRegexPattern(var->parsed_value), &capture));
  EXPECT_EQ(capture, "abc/123/def");
}

TEST(InternalRegexGen, VariableRegexNamedCapture) {
  re2::StringPiece kPattern = "abc";
  absl::StatusOr<ParsedResult<Variable>> var = consumeVariable("{var=*}");
  ASSERT_OK(var);

  RE2 regex = RE2(toRegexPattern(var->parsed_value));
  ASSERT_EQ(regex.NumberOfCapturingGroups(), 1);

  // Full matched string + capture groups
  std::vector<re2::StringPiece> captures(2);
  ASSERT_TRUE(regex.Match(kPattern, /*startpos=*/0, /*endpos=*/kPattern.size(), RE2::ANCHOR_BOTH,
                          captures.data(), captures.size()));

  // Index 0 would be the full text of the matched string.
  EXPECT_EQ(kPattern, captures[0]);
  // Get the pattern matched with the named capture group.
  EXPECT_EQ(kPattern, captures.at(regex.NamedCapturingGroups().at("var")));
}

TEST(InternalRegexGen, ParsedURLPatternToRegex) {
  absl::StatusOr<ParsedUrlPattern> pattern =
      parseURLPatternSyntax("/abc/*/{var1}/def/{var2=*/ghi/**}.jkl");
  ASSERT_OK(pattern);

  std::string var1_capture;
  std::string var2_capture;
  EXPECT_TRUE(RE2::FullMatch("/abc/123/456/def/789/ghi/%20/($).jkl",
                             toRegexPattern(pattern.value()), &var1_capture, &var2_capture));
  EXPECT_EQ(var1_capture, "456");
  EXPECT_EQ(var2_capture, "789/ghi/%20/($)");
}

struct GenPatternTestCase {
  GenPatternTestCase(std::string request_path, std::string url_pattern,
                     std::vector<std::pair<std::string, std::string>> capture_pairs)
      : path(request_path), pattern(url_pattern), captures(capture_pairs) {}
  std::string path;
  std::string pattern;
  std::vector<std::pair<std::string, std::string>> captures;
};

class GenPatternRegexWithMatch : public testing::TestWithParam<struct GenPatternTestCase> {
protected:
  const std::string& request_path() const { return GetParam().path; }
  const std::string& url_pattern() const { return GetParam().pattern; }
  std::vector<std::pair<std::string, std::string>> const var_values() {
    return GetParam().captures;
  }
};

INSTANTIATE_TEST_SUITE_P(
    GenPatternRegexWithMatchTestSuite, GenPatternRegexWithMatch,
    testing::Values(
        GenPatternTestCase("/media/1234/manifest.m3u8", "/**.m3u8", {}),
        GenPatternTestCase("/manifest.mpd", "/**.mpd", {}),
        GenPatternTestCase("/media/1234/manifest.m3u8", "/{path=**}.m3u8",
                           {{"path", "media/1234/manifest"}}),
        GenPatternTestCase("/foo/12314341/format/123/hls/segment_0000000001.ts", "/{foo}/**.ts",
                           {{"foo", "foo"}}),
        GenPatternTestCase("/media/eff456/ll-sd-out.js", "/media/{contentId=*}/**",
                           {{"contentId", "eff456"}}),
        GenPatternTestCase("/api/v1/1234/broadcasts/get", "/api/*/*/**", {}),
        GenPatternTestCase("/api/v1/1234", "/{version=api/*}/*", {{"version", "api/v1"}}),
        GenPatternTestCase("/api/v1/1234/", "/api/*/*/", {}),
        GenPatternTestCase("/api/v1/1234/broadcasts/get", "/api/*/{resource=*}/{method=**}",
                           {{"resource", "1234"}, {"method", "broadcasts/get"}}),
        GenPatternTestCase("/v1/broadcasts/12345/live", "/v1/**", {}),
        GenPatternTestCase("/media/us/en/12334/subtitle_enUS_00101.vtt",
                           "/media/{country}/{lang=*}/**", {{"country", "us"}, {"lang", "en"}}),
        GenPatternTestCase("/foo/bar/fo/fum/123", "/{foo}/{bar}/{fo}/{fum}/*",
                           {{"foo", "foo"}, {"bar", "bar"}, {"fo", "fo"}, {"fum", "fum"}}),
        GenPatternTestCase("/foo/bar/fo/fum/123", "/{foo=*}/{bar=*}/{fo=*}/{fum=*}/*",
                           {{"foo", "foo"}, {"bar", "bar"}, {"fo", "fo"}, {"fum", "fum"}}),
        GenPatternTestCase("/media/1234/hls/1001011.m3u8", "/media/{id=*}/**", {{"id", "1234"}}),
        GenPatternTestCase("/media/1234/hls/1001011.m3u8", "/media/{contentId=**}",
                           {{"contentId", "1234/hls/1001011.m3u8"}}),
        GenPatternTestCase("/api/v1/projects/my-project/locations/global/edgeCacheOrigins/foo",
                           "/api/{version}/projects/{project}/locations/{location}/{resource}/"
                           "{name}",
                           {{"version", "v1"},
                            {"project", "my-project"},
                            {"location", "global"},
                            {"resource", "edgeCacheOrigins"},
                            {"name", "foo"}}),
        GenPatternTestCase("/api/v1/foo/bar/baz/", "/api/{version=*}/{url=**}",
                           {{"version", "v1"}, {"url", "foo/bar/baz/"}}),
        GenPatternTestCase("/api/v1/v2/v3", "/api/{VERSION}/{version}/{verSION}",
                           {{"VERSION", "v1"}, {"version", "v2"}, {"verSION", "v3"}})));

TEST_P(GenPatternRegexWithMatch, WithCapture) {
  absl::StatusOr<ParsedUrlPattern> pattern = parseURLPatternSyntax(url_pattern());
  ASSERT_OK(pattern);

  RE2 regex = RE2(toRegexPattern(pattern.value()));
  ASSERT_TRUE(regex.ok()) << regex.error();
  ASSERT_EQ(regex.NumberOfCapturingGroups(), var_values().size());

  int capture_num = regex.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> captures(capture_num);
  ASSERT_TRUE(regex.Match(request_path(), /*startpos=*/0,
                          /*endpos=*/request_path().size(), RE2::ANCHOR_BOTH, captures.data(),
                          captures.size()));

  EXPECT_EQ(captures[0], toStringPiece(request_path()));

  for (const auto& [name, value] : var_values()) {
    int capture_index = regex.NamedCapturingGroups().at(name);
    ASSERT_GE(capture_index, 0);
    EXPECT_EQ(captures.at(capture_index), value);
  }
}

class GenPatternRegexWithoutMatch
    : public testing::TestWithParam<std::tuple<std::string, std::string>> {
protected:
  const std::string& request_path() const { return std::get<0>(GetParam()); }
  const std::string& url_pattern() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(GenPatternRegexWithoutMatchTestSuite, GenPatternRegexWithoutMatch,
                         testing::ValuesIn(std::vector<std::tuple<std::string, std::string>>(
                             {{"/media/12345/f/123/s00002.m4s", "/media/*.m4s"},
                              {"/media/eff456/ll-sd-out.js", "/media/*"},
                              {"/api/v1/1234/", "/api/*/v1/*"},
                              {"/api/v1/1234/broadcasts/get", "/api/*/{resource=*}/{method=*}"},
                              {"/api/v1/1234/", "/api/*/v1/**"},
                              {"/api/*/1234/", "/api/*/1234/"}})));

TEST_P(GenPatternRegexWithoutMatch, WithCapture) {
  absl::StatusOr<ParsedUrlPattern> pattern = parseURLPatternSyntax(url_pattern());
  ASSERT_OK(pattern);

  RE2 regex = RE2(toRegexPattern(pattern.value()));
  ASSERT_TRUE(regex.ok()) << regex.error();

  EXPECT_FALSE(regex.Match(request_path(), /*startpos=*/0,
                           /*endpos=*/request_path().size(), RE2::ANCHOR_BOTH, nullptr, 0));
}

} // namespace
} // namespace PatternTemplateInternal
} // namespace PatternTemplate
} // namespace Envoy
