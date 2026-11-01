#include <memory>

#include "source/extensions/matching/input_matchers/uri_template/matcher.h"

#include "gtest/gtest.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchResult;

class UriTemplateInputMatcherTest : public testing::Test {
protected:
  MatchResult matchPath(absl::string_view path) {
    return matcher_.match(DataInputGetResult::CreateStringView(path));
  }

  // The character classes exclude ? and # so the match depends on removing the query and fragment.
  UriTemplateInputMatcher matcher_{std::make_shared<const RE2>("/api/[a-z0-9]+/items/[0-9]+")};
};

TEST_F(UriTemplateInputMatcherTest, IgnoresQueryAndFragment) {
  EXPECT_EQ(matchPath("/api/v1/items/123?limit=1"), MatchResult::Matched);
  EXPECT_EQ(matchPath("/api/v1/items/123#top"), MatchResult::Matched);
}

TEST_F(UriTemplateInputMatcherTest, DoesNotMatchOtherPaths) {
  EXPECT_EQ(matchPath("/api/v1/items"), MatchResult::NoMatch);
  EXPECT_EQ(matchPath("/api/v1/items/123/extra"), MatchResult::NoMatch);
}

TEST_F(UriTemplateInputMatcherTest, DoesNotMatchMissingInput) {
  EXPECT_EQ(matcher_.match(DataInputGetResult::NoData()), MatchResult::NoMatch);
}

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
