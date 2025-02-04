#include <memory>

#include "source/extensions/filters/http/cache/stats.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CacheStatsTest : public ::testing::Test {
protected:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::unique_ptr<CacheFilterStats> stats_ = generateStats(context_.scope(), "fake.cache");
};

MATCHER_P(OptCounterHasValue, m, "") {
  return testing::ExplainMatchResult(
      testing::Optional(
          testing::Property("get", &std::reference_wrapper<const Stats::Counter>::get,
                            testing::Property("value", &Envoy::Stats::Counter::value, m))),
      arg, result_listener);
}

MATCHER_P(OptCounterHasName, m, "") {
  return testing::ExplainMatchResult(
      testing::Optional(testing::Property(
          "get", &std::reference_wrapper<const Stats::Counter>::get,
          testing::Property("tagExtractedName", &Envoy::Stats::Counter::tagExtractedName, m))),
      arg, result_listener);
}

MATCHER_P2(OptCounterIs, name, value, "") {
  return testing::ExplainMatchResult(
      testing::AllOf(OptCounterHasName(name), OptCounterHasValue(value)), arg, result_listener);
}

TEST_F(CacheStatsTest, StatsAreConstructedCorrectly) {
  // 4 for hit
  stats_->incForStatus(CacheEntryStatus::Hit);
  stats_->incForStatus(CacheEntryStatus::FoundNotModified);
  stats_->incForStatus(CacheEntryStatus::Streamed);
  stats_->incForStatus(CacheEntryStatus::ValidatedFree);
  Stats::CounterOptConstRef hits =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.hit");
  EXPECT_THAT(hits, OptCounterIs("cache.event", 4));
  // 2 for miss
  stats_->incForStatus(CacheEntryStatus::Miss);
  stats_->incForStatus(CacheEntryStatus::FailedValidation);
  Stats::CounterOptConstRef misses =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.miss");
  EXPECT_THAT(misses, OptCounterIs("cache.event", 2));
  // 1 for validated
  stats_->incForStatus(CacheEntryStatus::Validated);
  Stats::CounterOptConstRef validates =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.validate");
  EXPECT_THAT(validates, OptCounterIs("cache.event", 1));

  stats_->incForStatus(CacheEntryStatus::Uncacheable);
  Stats::CounterOptConstRef uncacheables = context_.store_.findCounterByString(
      "cache.event.cache_label.fake_cache.event_type.uncacheable");
  EXPECT_THAT(uncacheables, OptCounterIs("cache.event", 1));

  stats_->incForStatus(CacheEntryStatus::UpstreamReset);
  Stats::CounterOptConstRef upstream_resets = context_.store_.findCounterByString(
      "cache.event.cache_label.fake_cache.event_type.upstream_reset");
  EXPECT_THAT(upstream_resets, OptCounterIs("cache.event", 1));

  stats_->incForStatus(CacheEntryStatus::LookupError);
  Stats::CounterOptConstRef lookup_errors = context_.store_.findCounterByString(
      "cache.event.cache_label.fake_cache.event_type.lookup_error");
  EXPECT_THAT(lookup_errors, OptCounterIs("cache.event", 1));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
