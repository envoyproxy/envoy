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
using testing::HasSubstr;

// Capture regex for /{var1}/{var2}/{var3}/{var4}/{var5}
static constexpr absl::string_view kCaptureRegex = "/(?P<var1>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var2>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var3>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var4>[a-zA-Z0-9-._~%!$&'()+,;:@]+)/"
                                                   "(?P<var5>[a-zA-Z0-9-._~%!$&'()+,;:@]+)";

TEST(ConvertPathPattern, ValidPattern) {
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/abc"), IsOkAndHolds("/abc"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/**.mpd"),
              IsOkAndHolds("/[a-zA-Z0-9-._~%!$&'()+,;:@=/]*\\.mpd"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/api/*/{resource=*}/{method=**}"),
              IsOkAndHolds("/api/[a-zA-Z0-9-._~%!$&'()+,;:@=]+/"
                           "(?P<resource>[a-zA-Z0-9-._~%!$&'()+,;:@=]+)/"
                           "(?P<method>[a-zA-Z0-9-._~%!$&'()+,;:@=/]*)"));
  EXPECT_THAT(convertPathPatternSyntaxToRegex("/api/{VERSION}/{version}/{verSION}"),
              IsOkAndHolds("/api/(?P<VERSION>[a-zA-Z0-9-._~%!$&'()+,;:@=]+)/"
                           "(?P<version>[a-zA-Z0-9-._~%!$&'()+,;:@=]+)/"
                           "(?P<verSION>[a-zA-Z0-9-._~%!$&'()+,;:@=]+)"));
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

  const auto result = parseRewritePattern(pattern);
  EXPECT_OK(result);

  // The following is to exercise operator<< in ParseSegments.
  const auto& parsed_segments_vec = result.value();
  std::stringstream all_segments_str;
  for (const auto& parsed_segment : parsed_segments_vec) {
    all_segments_str << parsed_segment;
  }
  EXPECT_THAT(all_segments_str.str(), HasSubstr("kind = Literal, value ="));
  EXPECT_THAT(all_segments_str.str(), HasSubstr("kind = Variable, value ="));
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

class ParseRewriteSuccess : public testing::TestWithParam<std::string> {
protected:
  std::string rewritePattern() const { return GetParam(); }
};

TEST(ParseRewrite, InvalidRegex) {
  EXPECT_THAT(parseRewritePattern("/{var1}", "+[abc"), StatusIs(absl::StatusCode::kInternal));
}

INSTANTIATE_TEST_SUITE_P(ParseRewriteSuccessTestSuite, ParseRewriteSuccess,
                         testing::Values("/static", "/{var1}", "/{var1}", "/{var1}/{var1}/{var1}",
                                         "/{var3}/{var1}/{var2}", "/{var3}/abc/def/{var2}.suffix",
                                         "/abc/{var1}/{var2}/def", "/{var1}/{var2}",
                                         "/{var1}-{var2}/bucket-{var3}.suffix"));

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

  absl::StatusOr<RewriteSegments> rewrite_segment =
      parseRewritePattern(rewritePattern(), regex.value());
  ASSERT_OK(rewrite_segment);
}

TEST_P(PathPatternMatchAndRewrite, IsValidMatchPattern) {
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo}").ok());
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo}/{doo}").ok());
  EXPECT_TRUE(isValidMatchPattern("/{hoo}/bar/{goo}").ok());

  EXPECT_FALSE(isValidMatchPattern("/foo//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("//bar/{goo}").ok());
  EXPECT_FALSE(isValidMatchPattern("/foo/bar/{goo}}").ok());
}

TEST_P(PathPatternMatchAndRewrite, IsValidPathGlobMatchPattern) {
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo=*}").ok());
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo=*}/{doo=*}").ok());
  EXPECT_TRUE(isValidMatchPattern("/{hoo=*}/bar/{goo=*}").ok());

  EXPECT_FALSE(isValidMatchPattern("/foo//bar/{goo=*}").ok());
  EXPECT_FALSE(isValidMatchPattern("//bar/{goo=*}").ok());
  EXPECT_FALSE(isValidMatchPattern("/foo/bar/{goo=*}}").ok());
}

TEST_P(PathPatternMatchAndRewrite, IsValidTextGlobMatchPattern) {
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo=**}").ok());
  EXPECT_TRUE(isValidMatchPattern("/foo/bar/{goo=**}/doo").ok());
  EXPECT_TRUE(isValidMatchPattern("/{foo=*}/bar/{goo=**}").ok());
  EXPECT_TRUE(isValidMatchPattern("/*/bar/**").ok());

  EXPECT_FALSE(isValidMatchPattern("/{foo=**}/bar/{goo=*}").ok());
  EXPECT_FALSE(isValidMatchPattern("/**/bar/*").ok());
}

TEST_P(PathPatternMatchAndRewrite, IsValidRewritePattern) {
  EXPECT_TRUE(isValidRewritePattern("/foo/bar/{goo}").ok());
  EXPECT_TRUE(isValidRewritePattern("/foo/bar/{goo}/{doo}").ok());
  EXPECT_TRUE(isValidRewritePattern("/{hoo}/bar/{goo}").ok());

  EXPECT_FALSE(isValidRewritePattern("/foo//bar/{goo}").ok());
  EXPECT_FALSE(isValidRewritePattern("//bar/{goo}").ok());
  EXPECT_FALSE(isValidRewritePattern("/foo/bar/{goo}}").ok());
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
