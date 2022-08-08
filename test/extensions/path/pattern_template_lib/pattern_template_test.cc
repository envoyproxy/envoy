#include <string>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/path/pattern_template_lib/pattern_template.h"
#include "source/extensions/path/pattern_template_lib/pattern_template_internal.h"

#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {

namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::Envoy::StatusHelpers::StatusIs;

// Capture regex for /{var1}/{var2}/{var3}/{var4}/{var5}
static constexpr absl::string_view kCaptureRegex = "/(?P<var1>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var2>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var3>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var4>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var5>[a-zA-Z0-9-._~%!$&'()+,;:@]+)";
static constexpr absl::string_view kMatchUrl = "/val1/val2/val3/val4/val5";

TEST(ConvertURLPattern, ValidPattern) {
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/abc"), IsOkAndHolds("/abc"));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/**.mpd"),
              IsOkAndHolds("/[a-zA-Z0-9-._~%!$&'()+,;:@/]*\\.mpd"));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/api/*/{resource=*}/{method=**}"),
              IsOkAndHolds("/api/[a-zA-Z0-9-._~%!$&'()+,;:@]+/"
                           "(?P<resource>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<method>[a-zA-Z0-9-._~%!$&'()+,;:@/]*)"));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/api/{VERSION}/{version}/{verSION}"),
              IsOkAndHolds("/api/(?P<VERSION>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<version>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<verSION>[a-zA-Z0-9-._~%!$&'()+,;:@]+)"));
}

TEST(ConvertURLPattern, InvalidPattern) {
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/api/v*/1234"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/media/**/*/**"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/\001\002\003\004\005\006\007"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertURLPatternSyntaxToRegex("/{var12345678901234=*}"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

class ParseRewriteHelperSuccess : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ParseRewriteHelperSuccessTestSuite, ParseRewriteHelperSuccess,
                         testing::Values("/{var1}", "/{var1}{var2}", "/{var1}-{var2}",
                                         "/abc/{var1}/def", "/{var1}/abd/{var2}",
                                         "/abc-def-{var1}/a/{var1}"));

TEST_P(ParseRewriteHelperSuccess, ParseRewriteHelperSuccessTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_OK(parseRewritePatternHelper(pattern));
}

class ParseRewriteHelperFailure : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ParseRewriteHelperFailureTestSuite, ParseRewriteHelperFailure,
                         testing::Values("{var1}", "/{{var1}}", "/}va1{", "var1}",
                                         "/{var1}?abc=123", "", "/{var1/var2}", "/{}", "/a//b"));

TEST_P(ParseRewriteHelperFailure, ParseRewriteHelperFailureTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_THAT(parseRewritePatternHelper(pattern), StatusIs(absl::StatusCode::kInvalidArgument));
}

class ParseRewriteSuccess : public testing::TestWithParam<std::pair<std::string, std::string>> {
protected:
  const std::string& rewritePattern() const { return std::get<0>(GetParam()); }
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments expectedProto() const {
    envoy::extensions::pattern_template::PatternTemplateRewriteSegments expected_proto;
    Envoy::TestUtility::loadFromYaml(std::get<1>(GetParam()), expected_proto);
    return expected_proto;
  }
};

TEST(ParseRewrite, InvalidRegex) {
  EXPECT_THAT(parseRewritePattern("/{var1}", "+[abc"), StatusIs(absl::StatusCode::kInternal));
}

INSTANTIATE_TEST_SUITE_P(ParseRewriteSuccessTestSuite, ParseRewriteSuccess,
                         testing::ValuesIn(std::vector<std::pair<std::string, std::string>>({
                             {"/static", R"EOF(segments: {literal: "/static"} )EOF"},
                             {"/{var1}", R"EOF(segments:
                          - literal: "/"
                          - var_index: 1)EOF"},
                             {"/{var1}", R"EOF(segments:
                          - literal: "/"
                          - var_index: 1)EOF"},
                             {"/{var1}/{var1}/{var1}", R"EOF(segments:
                                        - literal: "/"
                                        - var_index: 1
                                        - literal: "/"
                                        - var_index: 1
                                        - literal: "/"
                                        - var_index: 1)EOF"},
                             {"/{var3}/{var1}/{var2}", R"EOF(segments
                                        - literal: "/"
                                        - var_index: 3
                                        - literal: "/"
                                        - var_index: 1
                                        - literal: "/"
                                        - var_index: 2)EOF"},
                             {"/{var3}/abc/def/{var2}.suffix", R"EOF(segments:
                                                - literal: "/"
                                                - var_index: 3
                                                - literal: "/abc/def/"
                                                - var_index: 2
                                                - literal: ".suffix")EOF"},
                             {"/abc/{var1}/{var2}/def", R"EOF(segments
                                         - literal: "/abc/"
                                         - var_index: 1
                                         - literal: "/"
                                         - var_index: 2
                                         - literal: "/def")EOF"},
                             {"/{var1}{var2}", R"EOF(segments
                                - literal: "/"
                                - var_index: 1
                                - ar_index: 2)EOF"},
                             {"/{var1}-{var2}/bucket-{var3}.suffix", R"EOF(segments
                                                      - literal: "/"
                                                      - var_index: 1
                                                      - literal: "-"
                                                      - var_index: 2
                                                      - literal: "/bucket-"
                                                      - var_index: 3
                                                      - literal: ".suffix")EOF"},
                         })));

TEST_P(ParseRewriteSuccess, ParseRewriteSuccessTest) {
  absl::StatusOr<envoy::extensions::pattern_template::PatternTemplateRewriteSegments> rewrite =
      parseRewritePattern(rewritePattern(), kCaptureRegex);
  ASSERT_OK(rewrite);
  // EXPECT_THAT(rewrite.value(), testing::EqualsProto(expected_proto()));
}

class ParseRewriteFailure : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ParseRewriteFailureTestSuite, ParseRewriteFailure,
                         testing::Values("{var1}", "/{var6}", "/{{var1}}", "/}va1{", "var1}",
                                         "/{var1}?abc=123", "", "/{var1/var2}", "/{}", "/a//b"));

TEST_P(ParseRewriteFailure, ParseRewriteFailureTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_THAT(parseRewritePattern(pattern, kCaptureRegex),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

class RewriteUrlTemplateSuccess
    : public testing::TestWithParam<std::pair<std::string, std::string>> {
protected:
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewriteProto() const {
    envoy::extensions::pattern_template::PatternTemplateRewriteSegments proto;
    Envoy::TestUtility::loadFromYaml(std::get<0>(GetParam()), proto);
    return proto;
  }
  const std::string& expectedRewrittenUrl() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(RewriteUrlTemplateSuccessTestSuite, RewriteUrlTemplateSuccess,
                         testing::ValuesIn(std::vector<std::pair<std::string, std::string>>(
                             {{R"EOF(segments: { literal: "/static" })EOF", "/static"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 1)EOF",
                               "/val1"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 1)EOF",
                               "/val1"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 1
              - literal: "/"
              - var_index: 1
              - literal: "/"
              - var_index: 1)EOF",
                               "/val1/val1/val1"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 3
              - literal: "/"
              - var_index: 1
              - literal: "/"
              - var_index: 2)EOF",
                               "/val3/val1/val2"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 3
              - literal: "/abc/def/"
              - var_index: 2
              - literal: ".suffix")EOF",
                               "/val3/abc/def/val2.suffix"},
                              {R"EOF(segments:
              - literal: "/"
              - var_index: 3
              - var_index: 2
              - literal: "."
              - var_index: 1)EOF",
                               "/val3val2.val1"},
                              {R"EOF(segments:
              - literal: "/abc/"
              - var_index: 1
              - literal: "/"
              - var_index: 5
              - literal: "/def")EOF",
                               "/abc/val1/val5/def"}})));

TEST_P(RewriteUrlTemplateSuccess, RewriteUrlTemplateSuccessTest) {
  absl::StatusOr<std::string> rewritten_url =
      rewriteURLTemplatePattern(kMatchUrl, kCaptureRegex, rewriteProto());
  ASSERT_OK(rewritten_url);
  EXPECT_EQ(rewritten_url.value(), expectedRewrittenUrl());
}

TEST(RewriteUrlTemplateFailure, BadRegex) {
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewrite_proto;

  const std::string yaml = R"EOF(
segments:
- literal: "/"
- var_index: 1
  )EOF";

  Envoy::TestUtility::loadFromYaml(yaml, rewrite_proto);

  EXPECT_THAT(rewriteURLTemplatePattern(kMatchUrl, "+/bad_regex", rewrite_proto),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(RewriteUrlTemplateFailure, RegexNoMatch) {
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewrite_proto;

  const std::string yaml = R"EOF(
segments:
- literal: "/"
- var_index: 1
  )EOF";

  Envoy::TestUtility::loadFromYaml(yaml, rewrite_proto);

  EXPECT_THAT(rewriteURLTemplatePattern(kMatchUrl, "/no_match_regex", rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RewriteUrlTemplateFailure, RegexCaptureIndexZero) {
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewrite_proto;

  const std::string yaml = R"EOF(
segments:
- literal: "/"
- var_index: 0
  )EOF";
  Envoy::TestUtility::loadFromYaml(yaml, rewrite_proto);

  EXPECT_THAT(rewriteURLTemplatePattern(kMatchUrl, kCaptureRegex, rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RewriteUrlTemplateFailure, RegexCaptureIndexAboveMaxCapture) {
  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewrite_proto;

  const std::string yaml = R"EOF(
segments:
- literal: "/"
- var_index: 6
  )EOF";

  Envoy::TestUtility::loadFromYaml(yaml, rewrite_proto);

  EXPECT_THAT(rewriteURLTemplatePattern(kMatchUrl, kCaptureRegex, rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

class URLPatternMatchAndRewrite
    : public testing::TestWithParam<
          std::tuple<std::string, std::string, std::string, std::string>> {
protected:
  const std::string& urlPattern() const { return std::get<0>(GetParam()); }
  const std::string& rewritePattern() const { return std::get<1>(GetParam()); }
  const std::string& matchUrl() const { return std::get<2>(GetParam()); }
  const std::string& expectedRewrittenUrl() const { return std::get<3>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    URLPatternMatchAndRewriteTestSuite, URLPatternMatchAndRewrite,
    testing::ValuesIn(std::vector<std::tuple<std::string, std::string, std::string, std::string>>(
        {{"/api/users/{id}/{path=**}", "/users/{id}/{path}", "/api/users/21334/profile.json",
          "/users/21334/profile.json"},
         {"/videos/*/{id}/{format}/{rendition}/{segment=**}.ts",
          "/{id}/{format}/{rendition}/{segment}.ts", "/videos/lib/132939/hls/13/segment_00001.ts",
          "/132939/hls/13/segment_00001.ts"},
         {"/region/{region}/bucket/{name}/{method=**}", "/{region}/bucket-{name}/{method}",
          "/region/eu/bucket/prod-storage/object.pdf", "/eu/bucket-prod-storage/object.pdf"},
         {"/region/{region}/bucket/{name}/{method=**}", "/{region}{name}/{method}",
          "/region/eu/bucket/prod-storage/object.pdf", "/euprod-storage/object.pdf"}})));

TEST_P(URLPatternMatchAndRewrite, URLPatternMatchAndRewriteTest) {
  absl::StatusOr<std::string> regex = convertURLPatternSyntaxToRegex(urlPattern());
  ASSERT_OK(regex);

  absl::StatusOr<envoy::extensions::pattern_template::PatternTemplateRewriteSegments>
      rewrite_proto = parseRewritePattern(rewritePattern(), regex.value());
  ASSERT_OK(rewrite_proto);

  absl::StatusOr<std::string> rewritten_url =
      rewriteURLTemplatePattern(matchUrl(), regex.value(), rewrite_proto.value());
  ASSERT_OK(rewritten_url);

  EXPECT_EQ(rewritten_url.value(), expectedRewrittenUrl());
}

} // namespace

} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
