#include "envoy/config/tap/v3/common.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap_matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

class TapMatcherTest : public testing::Test {
public:
  std::vector<MatcherPtr> matchers_;
  Matcher::MatchStatusVector statuses_;
  envoy::config::tap::v3::MatchPredicate config_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

TEST_F(TapMatcherTest, Any) {
  const std::string matcher_yaml =
      R"EOF(
any_match: true
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(1, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{true, false}), matchers_[0]->matchStatus(statuses_));
}

TEST_F(TapMatcherTest, Not) {
  const std::string matcher_yaml =
      R"EOF(
not_match:
  any_match: true
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(2, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
}

TEST_F(TapMatcherTest, AndMightChangeStatus) {
  const std::string matcher_yaml =
      R"EOF(
and_match:
  rules:
    - http_response_headers_match:
        headers:
          - name: bar
            exact_match: baz
)EOF";

  TestUtility::loadFromYaml(matcher_yaml, config_);
  buildMatcher(config_, matchers_);
  EXPECT_EQ(2, matchers_.size());
  statuses_.resize(matchers_.size());
  matchers_[0]->onNewStream(statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestHeaders(request_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpRequestTrailers(request_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, true}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseHeaders(response_headers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
  matchers_[0]->onHttpResponseTrailers(response_trailers_, statuses_);
  EXPECT_EQ((Matcher::MatchStatus{false, false}), matchers_[0]->matchStatus(statuses_));
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
