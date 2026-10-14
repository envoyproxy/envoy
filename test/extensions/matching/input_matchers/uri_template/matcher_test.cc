#include <memory>

#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"

#include "source/extensions/matching/input_matchers/uri_template/matcher.h"
#include "source/extensions/path/match/uri_template/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchResult;

class UriTemplateInputMatcherTest : public testing::Test {
protected:
  UriTemplateInputMatcherTest() {
    envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig config;
    config.set_path_template("/api/{version}/items/{id}");
    Extensions::UriTemplate::Match::UriTemplateMatcherFactory factory;
    matcher_ = std::make_unique<UriTemplateInputMatcher>(factory.createPathMatcher(config).value());
  }

  MatchResult matchPath(absl::string_view path) {
    return matcher_->match(DataInputGetResult::CreateStringView(path));
  }

  std::unique_ptr<UriTemplateInputMatcher> matcher_;
};

TEST_F(UriTemplateInputMatcherTest, IgnoresQueryAndFragment) {
  EXPECT_EQ(matchPath("/api/v1/items/123?limit=1"), MatchResult::Matched);
  EXPECT_EQ(matchPath("/api/v1/items/123#top"), MatchResult::Matched);
}

TEST_F(UriTemplateInputMatcherTest, DoesNotMatchOtherPaths) {
  EXPECT_EQ(matchPath("/api/v1/items"), MatchResult::NoMatch);
}

TEST_F(UriTemplateInputMatcherTest, DoesNotMatchMissingInput) {
  EXPECT_EQ(matcher_->match(DataInputGetResult::NoData()), MatchResult::NoMatch);
}

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
