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
  envoy::type::matcher::StringMatcher* inclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns();
  }
  envoy::type::matcher::StringMatcher* exclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns();
  }

  void initRegex() { stats_matcher_impl_ = std::make_unique<StatsMatcherImpl>(stats_config_); }

  envoy::config::metrics::v2::StatsConfig stats_config_;

protected:
  void expectAccepted(std::vector<std::string> expected_to_pass) {
    for (const auto& stat_name : expected_to_pass) {
      EXPECT_THAT(stats_matcher_impl_->rejects(stat_name), IsFalse());
    }
  }
  void expectDenied(std::vector<std::string> expected_to_fail) {
    for (const auto& stat_name : expected_to_fail) {
      EXPECT_THAT(stats_matcher_impl_->rejects(stat_name), IsTrue());
    }
  }

private:
  std::unique_ptr<StatsMatcherImpl> stats_matcher_impl_;
};

TEST_F(StatsMatcherTest, CheckDefault) {
  // With no set fields, everything should be allowed through.
  initRegex();
  expectAccepted({"foo", "bar", "foo.bar", "foo.bar.baz", "foobarbaz"});
}

// Across-the-board matchers.

TEST_F(StatsMatcherTest, CheckIncludeAll) {
  inclusionList()->set_regex(".*");
  initRegex();
  expectAccepted({"foo", "bar", "foo.bar", "foo.bar.baz"});
}

TEST_F(StatsMatcherTest, CheckExcludeAll) {
  exclusionList()->set_regex(".*");
  initRegex();
  expectDenied({"foo", "bar", "foo.bar", "foo.bar.baz"});
}

// Single exact matchers.

TEST_F(StatsMatcherTest, CheckIncludeExact) {
  inclusionList()->set_regex("abc");
  initRegex();
  expectAccepted({"abc"});
  expectDenied({"abcd", "abc.d", "d.abc", "dabc", "ab", "ac", "abcc", "Abc", "aBc", "abC", "abc.",
                ".abc", "ABC"});
}

TEST_F(StatsMatcherTest, CheckExcludeExact) {
  exclusionList()->set_exact("abc");
  initRegex();
  expectAccepted({"abcd", "abc.d", "d.abc", "dabc", "ab", "ac", "abcc", "Abc", "aBc", "abC", "abc.",
                  ".abc", "ABC"});
  expectDenied({"abc"});
}

// Single prefix matchers.

TEST_F(StatsMatcherTest, CheckIncludePrefix) {
  inclusionList()->set_prefix("abc");
  initRegex();
  expectAccepted({"abc", "abc.foo", "abcfoo"});
  expectDenied({"ABC", "ABC.foo", "ABCfoo", "foo", "abb", "a.b.c", "_abc", "foo.abc", "fooabc"});
}

TEST_F(StatsMatcherTest, CheckExcludePrefix) {
  exclusionList()->set_prefix("abc");
  initRegex();
  expectAccepted({"ABC", "ABC.foo", "ABCfoo", "foo", "abb", "a.b.c", "_abc", "foo.abc", "fooabc"});
  expectDenied({"abc", "abc.foo", "abcfoo"});
}

// Single suffix matchers.

TEST_F(StatsMatcherTest, CheckIncludeSuffix) {
  inclusionList()->set_suffix("abc");
  initRegex();
  expectAccepted({"abc", "foo.abc", "fooabc"});
  expectDenied({"ABC", "foo.ABC", "fooABC", "foo", "abb", "a.b.c", "abc_", "abc.foo", "abcfoo"});
}

TEST_F(StatsMatcherTest, CheckExcludeSuffix) {
  exclusionList()->set_suffix("abc");
  initRegex();
  expectAccepted({"ABC", "foo.ABC", "fooABC", "foo", "abb", "a.b.c", "abc_", "abc.foo", "abcfoo"});
  expectDenied({"abc", "foo.abc", "fooabc"});
}

// Single regex matchers.

TEST_F(StatsMatcherTest, CheckIncludeRegex) {
  inclusionList()->set_regex(".*envoy.*");
  initRegex();
  expectAccepted({"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"});
  expectDenied({"foo", "Envoy", "EnvoyProxy"});
}

TEST_F(StatsMatcherTest, CheckExcludeRegex) {
  exclusionList()->set_regex(".*envoy.*");
  initRegex();
  expectAccepted({"foo", "Envoy", "EnvoyProxy"});
  expectDenied({"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"});
}

// Multiple exact matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeExact) {
  inclusionList()->set_exact("foo");
  inclusionList()->set_exact("bar");
  initRegex();
  expectAccepted({"foo", "bar"});
  expectDenied({"foobar", "barfoo", "fo", "ba", "foo.bar"});
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeExact) {
  exclusionList()->set_exact("foo");
  exclusionList()->set_exact("bar");
  initRegex();
  expectAccepted({"foobar", "barfoo", "fo", "ba", "foo.bar"});
  expectDenied({"foo", "bar"});
}

// Multiple prefix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludePrefix) {
  inclusionList()->set_prefix("foo");
  inclusionList()->set_prefix("bar");
  initRegex();
  expectAccepted({"foo", "foo.abc", "bar", "bar.abc"});
  expectDenied({".foo", "abc.foo", "BAR", "_bar"});
}

TEST_F(StatsMatcherTest, CheckMultipleExcludePrefix) {
  exclusionList()->set_prefix("foo");
  exclusionList()->set_prefix("bar");
  initRegex();
  expectAccepted({".foo", "abc.foo", "BAR", "_bar"});
  expectDenied({"foo", "foo.abc", "bar", "bar.abc"});
}

// Multiple suffix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeSuffix) {
  inclusionList()->set_suffix("spam");
  inclusionList()->set_suffix("eggs");
  initRegex();
  expectAccepted(
      {"requests.for.spam", "requests.for.eggs", "spam", "eggs", "cannedspam", "fresheggs"});
  expectDenied({"Spam", "EGGS", "spam_", "eggs_"});
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeSuffix) {
  exclusionList()->set_suffix("spam");
  exclusionList()->set_suffix("eggs");
  initRegex();
  expectAccepted({"Spam", "EGGS", "spam_", "eggs_"});
  expectDenied(
      {"requests.for.spam", "requests.for.eggs", "spam", "eggs", "cannedspam", "fresheggs"});
}

// Multiple regex matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeRegex) {
  inclusionList()->set_regex(".*envoy.*");
  inclusionList()->set_regex(".*absl.*");
  initRegex();
  expectAccepted({"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"});
  expectDenied({"Abseil", "EnvoyProxy"});
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeRegex) {
  exclusionList()->set_regex(".*envoy.*");
  exclusionList()->set_regex(".*absl.*");
  initRegex();
  expectAccepted({"Abseil", "EnvoyProxy"});
  expectDenied({"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"});
}

// Multiple prefix/suffix/regex matchers.
//
// Matchers are "any_of", so strings matching any of the rules are expected to pass or fail,
// whichever the case may be.

TEST_F(StatsMatcherTest, CheckMultipleAssortedInclusionMatchers) {
  inclusionList()->set_regex(".*envoy.*");
  inclusionList()->set_suffix("requests");
  inclusionList()->set_exact("regex");
  initRegex();
  expectAccepted({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"});
  expectDenied({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"});
}

TEST_F(StatsMatcherTest, CheckMultipleAssortedExclusionMatchers) {
  exclusionList()->set_regex(".*envoy.*");
  exclusionList()->set_suffix("requests");
  exclusionList()->set_exact("regex");
  initRegex();
  expectAccepted({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"});
  expectDenied({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"});
}

} // namespace Stats
} // namespace Envoy
