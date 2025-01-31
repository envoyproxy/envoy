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

TEST_F(CacheStatsTest, StatsAreConstructedCorrectly) {
  // 4 for hit
  stats_->incForStatus(CacheEntryStatus::Hit);
  stats_->incForStatus(CacheEntryStatus::FoundNotModified);
  stats_->incForStatus(CacheEntryStatus::Streamed);
  stats_->incForStatus(CacheEntryStatus::ValidatedFree);
  Stats::CounterOptConstRef hits =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.hit");
  EXPECT_THAT(hits, OptCounterHasValue(4));
  EXPECT_THAT(hits->get().tagExtractedName(), "cache.event");
  // 2 for miss
  stats_->incForStatus(CacheEntryStatus::Miss);
  stats_->incForStatus(CacheEntryStatus::FailedValidation);
  Stats::CounterOptConstRef misses =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.miss");
  EXPECT_THAT(misses, OptCounterHasValue(2));
  EXPECT_THAT(misses->get().tagExtractedName(), "cache.event");
  // 1 for validated
  stats_->incForStatus(CacheEntryStatus::Validated);
  Stats::CounterOptConstRef validates =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.validate");
  EXPECT_THAT(validates, OptCounterHasValue(1));
  EXPECT_THAT(validates->get().tagExtractedName(), "cache.event");
  // 3 for skip
  stats_->incForStatus(CacheEntryStatus::Uncacheable);
  stats_->incForStatus(CacheEntryStatus::UpstreamReset);
  stats_->incForStatus(CacheEntryStatus::LookupError);
  Stats::CounterOptConstRef skips =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.skip");
  EXPECT_THAT(skips, OptCounterHasValue(3));
  EXPECT_THAT(skips->get().tagExtractedName(), "cache.event");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
