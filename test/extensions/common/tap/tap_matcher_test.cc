#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap_matcher.h"

#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

class TapMatcherTest : public TestBase {
public:
  std::vector<MatcherPtr> matchers_;
  std::vector<bool> statuses_;
  envoy::service::tap::v2alpha::MatchPredicate config_;
  Http::TestHeaderMapImpl headers_;
};

TEST_F(TapMatcherTest, Any) {
  const std::string matcher_yaml =
      R"EOF(
any_match: true
)EOF";

  MessageUtil::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(1, matchers_.size());
  statuses_.resize(matchers_.size());
  EXPECT_TRUE(matchers_[0]->onNewStream(statuses_));
  EXPECT_TRUE(matchers_[0]->onHttpRequestHeaders(headers_, statuses_));
  EXPECT_TRUE(matchers_[0]->onHttpResponseHeaders(headers_, statuses_));
  EXPECT_TRUE(matchers_[0]->matches(statuses_));
}

TEST_F(TapMatcherTest, Not) {
  const std::string matcher_yaml =
      R"EOF(
not_match:
  any_match: true
)EOF";

  MessageUtil::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(2, matchers_.size());
  statuses_.resize(matchers_.size());
  EXPECT_FALSE(matchers_[0]->onNewStream(statuses_));
  EXPECT_FALSE(matchers_[0]->onHttpRequestHeaders(headers_, statuses_));
  EXPECT_FALSE(matchers_[0]->onHttpResponseHeaders(headers_, statuses_));
  EXPECT_FALSE(matchers_[0]->matches(statuses_));
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
