#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/stats/stats_matcher_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatsMatcherTest : public testing::Test {
protected:
  StatsMatcherTest() : pool_(symbol_table_) {}

  envoy::type::matcher::v3::StringMatcher* inclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns();
  }
  envoy::type::matcher::v3::StringMatcher* exclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns();
  }
  void rejectAll(const bool should_reject) {
    stats_config_.mutable_stats_matcher()->set_reject_all(should_reject);
  }
  void initMatcher() {
    stats_matcher_impl_ = std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_);
  }
  void expectAccepted(const std::vector<std::string>& expected_to_pass) {
    for (const auto& stat_name : expected_to_pass) {
      EXPECT_FALSE(stats_matcher_impl_->rejects(pool_.add(stat_name))) << "Accepted: " << stat_name;
    }
  }
  void expectDenied(const std::vector<std::string>& expected_to_fail) {
    for (const auto& stat_name : expected_to_fail) {
      EXPECT_TRUE(stats_matcher_impl_->rejects(pool_.add(stat_name))) << "Rejected: " << stat_name;
    }
  }

  SymbolTableImpl symbol_table_;
  StatNamePool pool_;
  std::unique_ptr<StatsMatcherImpl> stats_matcher_impl_;

private:
  envoy::config::metrics::v3::StatsConfig stats_config_;
};

TEST_F(StatsMatcherTest, CheckDefault) {
  // With no set fields, everything should be allowed through.
  initMatcher();
  expectAccepted({"foo", "bar", "foo.bar", "foo.bar.baz", "foobarbaz"});
  EXPECT_TRUE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Across-the-board matchers.

TEST_F(StatsMatcherTest, CheckRejectAll) {
  // With reject_all, nothing should be allowed through.
  rejectAll(true);
  initMatcher();
  expectDenied({"foo", "bar", "foo.bar", "foo.bar.baz", "foobarbaz"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_TRUE(stats_matcher_impl_->rejectsAll());
  EXPECT_EQ(StatsMatcher::FastResult::Rejects, stats_matcher_impl_->fastRejects(StatName()));
}

TEST_F(StatsMatcherTest, CheckNotRejectAll) {
  // With !reject_all, everything should be allowed through.
  rejectAll(false);
  initMatcher();
  expectAccepted({"foo", "bar", "foo.bar", "foo.bar.baz", "foobarbaz"});
  EXPECT_TRUE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
  EXPECT_EQ(StatsMatcher::FastResult::NoMatch, stats_matcher_impl_->fastRejects(StatName()));
}

TEST_F(StatsMatcherTest, CheckIncludeAll) {
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  initMatcher();
  expectAccepted({"foo", "bar", "foo.bar", "foo.bar.baz"});
  // It really does accept all, but the impl doesn't know it.
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckExcludeAll) {
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  initMatcher();
  expectDenied({"foo", "bar", "foo.bar", "foo.bar.baz"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Single exact matchers.

TEST_F(StatsMatcherTest, CheckIncludeExact) {
  inclusionList()->set_exact("abc");
  initMatcher();
  expectAccepted({"abc"});
  expectDenied({"abcd", "abc.d", "d.abc", "dabc", "ab", "ac", "abcc", "Abc", "aBc", "abC", "ABC"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckExcludeExact) {
  exclusionList()->set_exact("abc");
  initMatcher();
  expectAccepted(
      {"abcd", "abc.d", "d.abc", "dabc", "ab", "ac", "abcc", "Abc", "aBc", "abC", "ABC"});
  expectDenied({"abc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Single prefix matchers.

TEST_F(StatsMatcherTest, CheckIncludePrefix) {
  inclusionList()->set_prefix("abc");
  initMatcher();
  expectAccepted({"abc", "abc.foo", "abcfoo"});
  expectDenied({"ABC", "ABC.foo", "ABCfoo", "foo", "abb", "a.b.c", "_abc", "foo.abc", "fooabc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckIncludePrefixDot) {
  inclusionList()->set_prefix("abc.");
  initMatcher();
  expectAccepted({"abc", "abc.foo"});
  expectDenied(
      {"abcfoo", "ABC", "ABC.foo", "ABCfoo", "foo", "abb", "a.b.c", "_abc", "foo.abc", "fooabc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
  EXPECT_EQ(StatsMatcher::FastResult::Matches,
            stats_matcher_impl_->fastRejects(pool_.add("abc.foo")));
}

TEST_F(StatsMatcherTest, CheckExcludePrefix) {
  exclusionList()->set_prefix("abc");
  initMatcher();
  expectAccepted({"ABC", "ABC.foo", "ABCfoo", "foo", "abb", "a.b.c", "_abc", "foo.abc", "fooabc"});
  expectDenied({"abc", "abc.foo", "abcfoo"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Single suffix matchers.

TEST_F(StatsMatcherTest, CheckIncludeSuffix) {
  inclusionList()->set_suffix("abc");
  initMatcher();
  expectAccepted({"abc", "foo.abc", "fooabc"});
  expectDenied({"ABC", "foo.ABC", "fooABC", "foo", "abb", "a.b.c", "abc_", "abc.foo", "abcfoo"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckExcludeSuffix) {
  exclusionList()->set_suffix("abc");
  initMatcher();
  expectAccepted({"ABC", "foo.ABC", "fooABC", "foo", "abb", "a.b.c", "abc_", "abc.foo", "abcfoo"});
  expectDenied({"abc", "foo.abc", "fooabc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Single regex matchers.

TEST_F(StatsMatcherTest, CheckIncludeRegex) {
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  initMatcher();
  expectAccepted({"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"});
  expectDenied({"foo", "Envoy", "EnvoyProxy"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckExcludeRegex) {
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  initMatcher();
  expectAccepted({"foo", "Envoy", "EnvoyProxy"});
  expectDenied({"envoy.matchers.requests", "stats.envoy.2xx", "regex.envoy.matchers"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Multiple exact matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeExact) {
  inclusionList()->set_exact("foo");
  inclusionList()->set_exact("bar");
  initMatcher();
  expectAccepted({"foo", "bar"});
  expectDenied({"foobar", "barfoo", "fo", "ba", "foo.bar"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeExact) {
  exclusionList()->set_exact("foo");
  exclusionList()->set_exact("bar");
  initMatcher();
  expectAccepted({"foobar", "barfoo", "fo", "ba", "foo.bar"});
  expectDenied({"foo", "bar"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Multiple prefix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludePrefix) {
  inclusionList()->set_prefix("foo");
  inclusionList()->set_prefix("bar");
  initMatcher();
  expectAccepted({"foo", "foo.abc", "bar", "bar.abc"});
  expectDenied({".foo", "abc.foo", "BAR", "_bar"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleExcludePrefix) {
  exclusionList()->set_prefix("foo");
  exclusionList()->set_prefix("bar");
  initMatcher();
  expectAccepted({".foo", "abc.foo", "BAR", "_bar"});
  expectDenied({"foo", "foo.abc", "bar", "bar.abc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Multiple suffix matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeSuffix) {
  inclusionList()->set_suffix("spam");
  inclusionList()->set_suffix("eggs");
  initMatcher();
  expectAccepted(
      {"requests.for.spam", "requests.for.eggs", "spam", "eggs", "cannedspam", "fresheggs"});
  expectDenied({"Spam", "EGGS", "spam_", "eggs_"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeSuffix) {
  exclusionList()->set_suffix("spam");
  exclusionList()->set_suffix("eggs");
  initMatcher();
  expectAccepted({"Spam", "EGGS", "spam_", "eggs_"});
  expectDenied(
      {"requests.for.spam", "requests.for.eggs", "spam", "eggs", "cannedspam", "fresheggs"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Multiple regex matchers.

TEST_F(StatsMatcherTest, CheckMultipleIncludeRegex) {
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*absl.*"));
  initMatcher();
  expectAccepted({"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"});
  expectDenied({"Abseil", "EnvoyProxy"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleExcludeRegex) {
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*absl.*"));
  initMatcher();
  expectAccepted({"Abseil", "EnvoyProxy"});
  expectDenied({"envoy.matchers.requests", "stats.absl.2xx", "absl.envoy.matchers"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

// Multiple prefix/suffix/regex matchers.
//
// Matchers are "any_of", so strings matching any of the rules are expected to pass or fail,
// whichever the case may be.

TEST_F(StatsMatcherTest, CheckMultipleAssortedInclusionMatchers) {
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  inclusionList()->set_suffix("requests");
  inclusionList()->set_exact("regex");
  initMatcher();
  expectAccepted({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"});
  expectDenied({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleAssortedExclusionMatchers) {
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  exclusionList()->set_suffix("requests");
  exclusionList()->set_exact("regex");
  initMatcher();
  expectAccepted({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"});
  expectDenied({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleAssortedInclusionMatchersWithPrefix) {
  inclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  inclusionList()->set_suffix("requests");
  inclusionList()->set_exact("regex");
  inclusionList()->set_prefix("prefix.");
  initMatcher();
  expectAccepted({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex",
                  "prefix", "prefix.foo"});
  expectAccepted({"prefix.envoy.matchers.requests", "prefix.requests.for.envoy",
                  "prefix.envoyrequests", "prefix.regex"});
  expectDenied({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

TEST_F(StatsMatcherTest, CheckMultipleAssortedExclusionMatchersWithPrefix) {
  exclusionList()->MergeFrom(TestUtility::createRegexMatcher(".*envoy.*"));
  exclusionList()->set_suffix("requests");
  exclusionList()->set_exact("regex");
  exclusionList()->set_prefix("prefix.");
  initMatcher();
  expectAccepted({"requestsEnvoy", "EnvoyProxy", "foo", "regex_etc", "prefixfoo"});
  expectDenied({"envoy.matchers.requests", "requests.for.envoy", "envoyrequests", "regex", "prefix",
                "prefix.foo", "prefix.requests.for.envoy"});
  EXPECT_FALSE(stats_matcher_impl_->acceptsAll());
  EXPECT_FALSE(stats_matcher_impl_->rejectsAll());
}

} // namespace Stats
} // namespace Envoy
