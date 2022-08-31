#include <string>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/path/uri_template_lib/uri_template.h"
#include "source/extensions/path/uri_template_lib/uri_template_internal.h"

#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {

namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::Envoy::StatusHelpers::StatusIs;

// Capture regex for /{var1}/{var2}/{var3}/{var4}/{var5}
static constexpr absl::string_view kCaptureRegex = "/(?P<var1>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var2>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var3>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var4>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var5>[a-zA-Z0-9-._~%!$&'()+,;:@]+)";
static constexpr absl::string_view kMatchPath = "/val1/val2/val3/val4/val5";

TEST(ConvertPathPattern, ValidPattern) {
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/abc"), IsOkAndHolds("/abc"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/**.mpd"),
              IsOkAndHolds("/[a-zA-Z0-9-._~%!$&'()+,;:@/]*\\.mpd"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/api/*/{resource=*}/{method=**}"),
              IsOkAndHolds("/api/[a-zA-Z0-9-._~%!$&'()+,;:@]+/"
                           "(?P<resource>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<method>[a-zA-Z0-9-._~%!$&'()+,;:@/]*)"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/api/{VERSION}/{version}/{verSION}"),
              IsOkAndHolds("/api/(?P<VERSION>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<version>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                           "(?P<verSION>[a-zA-Z0-9-._~%!$&'()+,;:@]+)"));
}

TEST(ConvertPathPattern, InvalidPattern) {
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/api/v*/1234"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/media/**/*/**"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/\001\002\003\004\005\006\007"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/{var12345678901234=*}"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/{var12345678901234=*"),
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

  EXPECT_OK(parseRewritePattern(pattern));
}

class ParseRewriteHelperFailure : public testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(ParseRewriteHelperFailureTestSuite, ParseRewriteHelperFailure,
                         testing::Values("{var1}", "/{{var1}}", "/}va1{", "var1}",
                                         "/{var1}?abc=123", "", "/{var1/var2}", "/{}", "/a//b"));

TEST_P(ParseRewriteHelperFailure, ParseRewriteHelperFailureTest) {
  std::string pattern = GetParam();
  SCOPED_TRACE(pattern);

  EXPECT_THAT(parseRewritePattern(pattern), StatusIs(absl::StatusCode::kInvalidArgument));
}

class ParseRewriteSuccess
    : public testing::TestWithParam<
          std::pair<std::string, std::vector<absl::variant<int, std::string>>>> {
protected:
  const std::string& rewritePattern() const { return std::get<0>(GetParam()); }
  RewriteSegments expectedProto() const {
    RewriteSegments expected_proto;
    expected_proto.segments = std::get<1>(GetParam());
    return expected_proto;
  }
};

TEST(ParseRewrite, InvalidRegex) {
  EXPECT_THAT(parseRewritePattern("/{var1}", "+[abc"), StatusIs(absl::StatusCode::kInternal));
}

INSTANTIATE_TEST_SUITE_P(
    ParseRewriteSuccessTestSuite, ParseRewriteSuccess,
    testing::ValuesIn(
        std::vector<std::pair<std::string, std::vector<absl::variant<int, std::string>>>>({
            {"/static",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/static")}},
            {"/{var1}",
             std::vector<absl::variant<int, std::string>>{absl::variant<int, std::string>("/"),
                                                          absl::variant<int, std::string>(1)}},
            {"/{var1}",
             std::vector<absl::variant<int, std::string>>{absl::variant<int, std::string>("/"),
                                                          absl::variant<int, std::string>(1)}},
            {"/{var1}/{var1}/{var1}",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1)}},
            {"/{var3}/{var1}/{var2}",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(3),
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(2)}},
            {"/{var3}/abc/def/{var2}.suffix",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(3),
                 absl::variant<int, std::string>("/abc/def/"), absl::variant<int, std::string>(2),
                 absl::variant<int, std::string>(".suffix")}},
            {"/abc/{var1}/{var2}/def",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/abc/"), absl::variant<int, std::string>(1),
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(2),
                 absl::variant<int, std::string>("/def")}},
            {"/{var1}/{var2}",
             std::vector<absl::variant<int, std::string>>{absl::variant<int, std::string>("/"),
                                                          absl::variant<int, std::string>(1),
                                                          absl::variant<int, std::string>(2)}},
            {"/{var1}-{var2}/bucket-{var3}.suffix",
             std::vector<absl::variant<int, std::string>>{
                 absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                 absl::variant<int, std::string>("-"), absl::variant<int, std::string>(2),
                 absl::variant<int, std::string>("/bucket-"), absl::variant<int, std::string>(3),
                 absl::variant<int, std::string>(".suffix")}},
        })));

TEST_P(ParseRewriteSuccess, ParseRewriteSuccessTest) {
  absl::StatusOr<RewriteSegments> rewrite = parseRewritePattern(rewritePattern(), kCaptureRegex);
  ASSERT_OK(rewrite);
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

class RewritePathTemplateSuccess
    : public testing::TestWithParam<
          std::pair<std::vector<absl::variant<int, std::string>>, std::string>> {
protected:
  RewriteSegments rewriteProto() const {
    RewriteSegments proto;
    proto.segments = std::get<0>(GetParam());
    return proto;
  }

  const std::string& expectedRewrittenPath() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    RewritePathTemplateSuccessTestSuite, RewritePathTemplateSuccess,
    testing::ValuesIn(
        std::vector<std::pair<std::vector<absl::variant<int, std::string>>, std::string>>(
            {{std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/static")},
              "/static"},
             {std::vector<absl::variant<int, std::string>>{absl::variant<int, std::string>("/"),
                                                           absl::variant<int, std::string>(1)},
              "/val1"},
             {std::vector<absl::variant<int, std::string>>{absl::variant<int, std::string>("/"),
                                                           absl::variant<int, std::string>(1)},
              "/val1"},
             {std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1)},
              "/val1/val1/val1"},
             {std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(3),
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1),
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(2)},
              "/val3/val1/val2"},
             {std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(3),
                  absl::variant<int, std::string>("/abc/def/"), absl::variant<int, std::string>(2),
                  absl::variant<int, std::string>(".suffix")},
              "/val3/abc/def/val2.suffix"},
             {std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(3),
                  absl::variant<int, std::string>(2), absl::variant<int, std::string>("."),
                  absl::variant<int, std::string>(1)},
              "/val3val2.val1"},
             {std::vector<absl::variant<int, std::string>>{
                  absl::variant<int, std::string>("/abc/"), absl::variant<int, std::string>(1),
                  absl::variant<int, std::string>("/"), absl::variant<int, std::string>(5),
                  absl::variant<int, std::string>("/def")},
              "/abc/val1/val5/def"}})));

TEST_P(RewritePathTemplateSuccess, RewritePathTemplateSuccessTest) {
  absl::StatusOr<std::string> rewritten_path =
      rewritePathTemplatePattern(kMatchPath, kCaptureRegex, rewriteProto());
  ASSERT_OK(rewritten_path);
  EXPECT_EQ(rewritten_path.value(), expectedRewrittenPath());
}

TEST(RewritePathTemplateFailure, BadRegex) {
  RewriteSegments rewrite_proto;
  rewrite_proto.segments = std::vector<absl::variant<int, std::string>>{
      absl::variant<int, std::string>("/static"), absl::variant<int, std::string>(1)};

  EXPECT_THAT(rewritePathTemplatePattern(kMatchPath, "+/bad_regex", rewrite_proto),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(RewritePathTemplateFailure, RegexNoMatch) {
  RewriteSegments rewrite_proto;
  rewrite_proto.segments = std::vector<absl::variant<int, std::string>>{
      absl::variant<int, std::string>("/"), absl::variant<int, std::string>(1)};

  EXPECT_THAT(rewritePathTemplatePattern(kMatchPath, "/no_match_regex", rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RewritePathTemplateFailure, RegexCaptureIndexZero) {
  RewriteSegments rewrite_proto;
  rewrite_proto.segments = std::vector<absl::variant<int, std::string>>{
      absl::variant<int, std::string>("/"), absl::variant<int, std::string>(0)};

  EXPECT_THAT(rewritePathTemplatePattern(kMatchPath, kCaptureRegex, rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RewritePathTemplateFailure, RegexCaptureIndexAboveMaxCapture) {
  RewriteSegments rewrite_proto;
  rewrite_proto.segments = std::vector<absl::variant<int, std::string>>{
      absl::variant<int, std::string>("/"), absl::variant<int, std::string>(6)};

  EXPECT_THAT(rewritePathTemplatePattern(kMatchPath, kCaptureRegex, rewrite_proto),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

class PathPatternMatchAndRewrite
    : public testing::TestWithParam<
          std::tuple<std::string, std::string, std::string, std::string>> {
protected:
  const std::string& pattern() const { return std::get<0>(GetParam()); }
  const std::string& rewritePattern() const { return std::get<1>(GetParam()); }
  const std::string& matchPath() const { return std::get<2>(GetParam()); }
  const std::string& expectedRewrittenPath() const { return std::get<3>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    PathPatternMatchAndRewriteTestSuite, PathPatternMatchAndRewrite,
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

TEST_P(PathPatternMatchAndRewrite, PathPatternMatchAndRewriteTest) {
  absl::StatusOr<std::string> regex = convertPathPatternSyntaxToRegex(pattern());
  ASSERT_OK(regex);

  absl::StatusOr<RewriteSegments> rewrite_proto =
      parseRewritePattern(rewritePattern(), regex.value());
  ASSERT_OK(rewrite_proto);

  absl::StatusOr<std::string> rewritten_path =
      rewritePathTemplatePattern(matchPath(), regex.value(), rewrite_proto.value());
  ASSERT_OK(rewritten_path);

  EXPECT_EQ(rewritten_path.value(), expectedRewrittenPath());
}

TEST_P(PathPatternMatchAndRewrite, IsValidMatchPattern) {
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo}").ok());
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo}/{doo}").ok());
  EXPECT_TRUE(isValidMatchPattern("/{hoo}/bar/{goo}").ok());

  EXPECT_FALSE(isValidMatchPattern("/foo//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("/foo/bar/{goo}}").ok());
}

TEST_P(PathPatternMatchAndRewrite, IsValidRewritePattern) {
  EXPECT_TRUE(isValidRewritePattern("/foo/bar/{goo}").ok());
  EXPECT_TRUE(isValidRewritePattern("/foo/bar/{goo}/{doo}").ok());
  EXPECT_TRUE(isValidRewritePattern("/{hoo}/bar/{goo}").ok());

  EXPECT_FALSE(isValidMatchPattern("/foo//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("/foo//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("/foo/bar/{goo}}").ok());
}

TEST_P(PathPatternMatchAndRewrite, IsValidSharedVariableSet) {
  EXPECT_TRUE(isValidSharedVariableSet("/foo/bar/{goo}", "/foo/bar/{goo}").ok());
  EXPECT_TRUE(isValidSharedVariableSet("/foo/bar/{goo}/{doo}", "/foo/bar/{doo}/{goo}").ok());
  EXPECT_TRUE(isValidSharedVariableSet("/bar/{goo}", "/bar/{goo}").ok());

  EXPECT_FALSE(isValidSharedVariableSet("/foo/bar/{goo}/{goo}", "/foo/{bar}").ok());
  EXPECT_FALSE(isValidSharedVariableSet("/foo/{goo}", "/foo/bar/").ok());
  EXPECT_FALSE(isValidSharedVariableSet("/foo/bar/{goo}", "/{foo}").ok());
}

} // namespace

} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
