#include "envoy/config/metrics/v2/stats.pb.h"

#include "common/stats/stats_matcher_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::IsFalse;
using testing::IsTrue;

namespace Envoy {
namespace Stats {

class StatsMatcherTest : public testing::Test {
public:
  StatsMatcherTest() {}
  void SetUp() override { stats_matcher_impl_ = std::make_unique<StatsMatcherImpl>(stats_config_); }
  void TearDown() override {
    for (const auto& stat_name : expected_to_pass_) {
      EXPECT_THAT(stats_matcher_impl_->rejects(stat_name), IsFalse());
    }
    for (const auto& stat_name : expected_to_fail_) {
      EXPECT_THAT(stats_matcher_impl_->rejects(stat_name), IsTrue());
    }
  }

  envoy::config::metrics::v2::StatsConfig stats_config_;

  std::vector<std::string> expected_to_pass_;
  std::vector<std::string> expected_to_fail_;

private:
  std::unique_ptr<StatsMatcherImpl> stats_matcher_impl_;
};

TEST_F(StatsMatcherTest, CheckDefault) {
  // With no set fields, everything should be allowed through.
  SetUp();
  expected_to_pass_ = {"foo", "bar", "foo.bar", "foo.bar.baz", "foobarbaz"};
}

// Across-the-board matchers.

TEST_F(StatsMatcherTest, CheckIncludeAll) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex(".*");
  SetUp();
  expected_to_pass_ = {"foo", "bar", "foo.bar", "foo.bar.baz"};
}

TEST_F(StatsMatcherTest, CheckExcludeAll) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(".*");
  SetUp();
  expected_to_fail_ = {"foo", "bar", "foo.bar", "foo.bar.baz"};
}

// Single exact matchers.

TEST_F(StatsMatcherTest, CheckIncludeExact) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex("abc");
  SetUp();
  expected_to_pass_ = {"abc"};
  expected_to_fail_ = {"abcd", "abc.d", "d.abc", "dabc", "ab",   "ac", "abcc",
                       "Abc",  "aBc",   "abC",   "abc.", ".abc", "ABC"};
}

TEST_F(StatsMatcherTest, CheckExcludeExact) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_exact("abc");
  SetUp();
  expected_to_pass_ = {"abcd", "abc.d", "d.abc", "dabc", "ab",   "ac", "abcc",
                       "Abc",  "aBc",   "abC",   "abc.", ".abc", "ABC"};
  expected_to_fail_ = {"abc"};
}

// Single prefix matchers.

TEST_F(StatsMatcherTest, CheckIncludePrefix) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_prefix(
      "abc");
  SetUp();
  expected_to_pass_ = {"abc", "abc.foo", "abcfoo"};
  expected_to_fail_ = {"ABC",   "ABC.foo", "ABCfoo",  "foo",   "abb",
                       "a.b.c", "_abc",    "foo.abc", "fooabc"};
}

TEST_F(StatsMatcherTest, CheckExcludePrefix) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "abc");
  SetUp();
  expected_to_pass_ = {"ABC",   "ABC.foo", "ABCfoo",  "foo",   "abb",
                       "a.b.c", "_abc",    "foo.abc", "fooabc"};
  expected_to_fail_ = {"abc", "abc.foo", "abcfoo"};
}

// Single suffix matchers.

TEST_F(StatsMatcherTest, CheckIncludeSuffix) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_suffix(
      "abc");
  SetUp();
  expected_to_pass_ = {"abc", "foo.abc", "fooabc"};
  expected_to_fail_ = {"ABC",   "foo.ABC", "fooABC",  "foo",   "abb",
                       "a.b.c", "abc_",    "abc.foo", "abcfoo"};
}

TEST_F(StatsMatcherTest, CheckExcludeSuffix) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_suffix(
      "abc");
  SetUp();
  expected_to_pass_ = {"ABC",   "foo.ABC", "fooABC",  "foo",   "abb",
                       "a.b.c", "abc_",    "abc.foo", "abcfoo"};
  expected_to_fail_ = {"abc", "foo.abc", "fooabc"};
}

// Single regex matchers.

TEST_F(StatsMatcherTest, CheckIncludeRegex) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  SetUp();
  expected_to_pass_ = {"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"};
  expected_to_fail_ = {"foo", "Envoy", "EnvoyProxy"};
}

TEST_F(StatsMatcherTest, CheckExcludeRegex) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  SetUp();
  expected_to_pass_ = {"foo", "Envoy", "EnvoyProxy"};
  expected_to_fail_ = {"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"};
}

// Multiple exact matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeExact) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_exact("foo");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_exact("bar");
  SetUp();
  expected_to_pass_ = {"foo", "bar"};
  expected_to_fail_ = {"foobar", "barfoo", "fo", "ba", "foo.bar"};
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeExact) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_exact("foo");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_exact("bar");
  SetUp();
  expected_to_pass_ = {"foobar", "barfoo", "fo", "ba", "foo.bar"};
  expected_to_fail_ = {"foo", "bar"};
}

// Multiple prefix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludePrefix) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_prefix(
      "foo");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_prefix(
      "bar");
  SetUp();
  expected_to_pass_ = {"foo", "foo.abc", "bar", "bar.abc"};
  expected_to_fail_ = {".foo", "abc.foo", "BAR", "_bar"};
}

TEST_F(StatsMatcherTest, CheckMultipleExcludePrefix) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "foo");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_prefix(
      "bar");
  SetUp();
  expected_to_pass_ = {".foo", "abc.foo", "BAR", "_bar"};
  expected_to_fail_ = {"foo", "foo.abc", "bar", "bar.abc"};
}

// Multiple suffix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeSuffix) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_suffix(
      "spam");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_suffix(
      "eggs");
  SetUp();
  expected_to_pass_ = {"requests.for.spam", "requests.for.eggs", "spam", "eggs",
                       "cannedspam",        "fresheggs"};
  expected_to_fail_ = {"Spam", "EGGS", "spam_", "eggs_"};
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeSuffix) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_suffix(
      "spam");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_suffix(
      "eggs");
  SetUp();
  expected_to_pass_ = {"Spam", "EGGS", "spam_", "eggs_"};
  expected_to_fail_ = {"requests.for.spam", "requests.for.eggs", "spam", "eggs",
                       "cannedspam",        "fresheggs"};
}

// Multiple regex matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeRegex) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex(
      ".*absl.*");
  SetUp();
  expected_to_pass_ = {"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"};
  expected_to_fail_ = {"Abseil", "EnvoyProxy"};
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeRegex) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(
      ".*absl.*");
  SetUp();
  expected_to_pass_ = {"Abseil", "EnvoyProxy"};
  expected_to_fail_ = {"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"};
}

// Multiple prefix/suffix/regex matchers.
//
// Matchers are "any_of", so strings matching any of the rules are expected to pass or fail,
// whichever the case may be.

TEST_F(StatsMatcherTest, CheckMultipleAssortedInclusionMatchers) {
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_suffix(
      "requests");
  stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns()->set_exact(
      "regex");
  SetUp();
  expected_to_pass_ = {"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"};
  expected_to_fail_ = {"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"};
}

TEST_F(StatsMatcherTest, CheckMultipleAssortedExclusionMatchers) {
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_regex(
      ".*envoy.*");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_suffix(
      "requests");
  stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns()->set_exact(
      "regex");
  SetUp();
  expected_to_pass_ = {"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"};
  expected_to_fail_ = {"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"};
}

} // namespace Stats
} // namespace Envoy
