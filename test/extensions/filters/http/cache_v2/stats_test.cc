#include <memory>

#include "source/extensions/filters/http/cache_v2/stats.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
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

MATCHER_P(OptGaugeHasValue, m, "") {
  return testing::ExplainMatchResult(
      testing::Optional(
          testing::Property("get", &std::reference_wrapper<const Stats::Gauge>::get,
                            testing::Property("value", &Envoy::Stats::Gauge::value, m))),
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
  stats_->incForStatus(CacheEntryStatus::Follower);
  stats_->incForStatus(CacheEntryStatus::ValidatedFree);
  Stats::CounterOptConstRef hits =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.hit");
  EXPECT_THAT(hits, OptCounterIs("cache.event", 4));
  // 1 for miss
  stats_->incForStatus(CacheEntryStatus::Miss);
  Stats::CounterOptConstRef misses =
      context_.store_.findCounterByString("cache.event.cache_label.fake_cache.event_type.miss");
  EXPECT_THAT(misses, OptCounterIs("cache.event", 1));
  // 1 for failed validation
  stats_->incForStatus(CacheEntryStatus::FailedValidation);
  Stats::CounterOptConstRef failed_validations = context_.store_.findCounterByString(
      "cache.event.cache_label.fake_cache.event_type.failed_validation");
  EXPECT_THAT(failed_validations, OptCounterIs("cache.event", 1));
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

  stats_->incCacheSessionsEntries();
  stats_->incCacheSessionsEntries();
  stats_->incCacheSessionsEntries();
  stats_->decCacheSessionsEntries();
  Stats::GaugeOptConstRef cache_sessions_entries =
      context_.store_.findGaugeByString("cache.cache_sessions_entries.cache_label.fake_cache");
  EXPECT_THAT(cache_sessions_entries, OptGaugeHasValue(2));

  stats_->incCacheSessionsSubscribers();
  stats_->incCacheSessionsSubscribers();
  stats_->incCacheSessionsSubscribers();
  stats_->subCacheSessionsSubscribers(2);
  Stats::GaugeOptConstRef cache_sessions_subscribers =
      context_.store_.findGaugeByString("cache.cache_sessions_subscribers.cache_label.fake_cache");
  EXPECT_THAT(cache_sessions_subscribers, OptGaugeHasValue(1));

  stats_->addUpstreamBufferedBytes(1024);
  stats_->subUpstreamBufferedBytes(512);
  Stats::GaugeOptConstRef upstream_buffered_bytes =
      context_.store_.findGaugeByString("cache.upstream_buffered_bytes.cache_label.fake_cache");
  EXPECT_THAT(upstream_buffered_bytes, OptGaugeHasValue(512));
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
