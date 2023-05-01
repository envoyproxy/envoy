#include "test/server/admin/stats_request_test_base.h"

namespace Envoy {
namespace Server {

class UngroupedStatsRequestTest : public StatsRequestTestBase<UngroupedStatsRequest> {
protected:
  std::unique_ptr<UngroupedStatsRequest> makeRequest(bool used_only, StatsFormat format,
                                                     StatsType type) {
    StatsParams params;
    params.used_only_ = used_only;
    params.type_ = type;
    params.format_ = format;
    return std::make_unique<UngroupedStatsRequest>(store_, params);
  }
};

TEST_F(UngroupedStatsRequestTest, Empty) {
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(UngroupedStatsRequestTest, OneCounter) {
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
}

TEST_F(UngroupedStatsRequestTest, OneGauge) {
  store_.rootScope()->gaugeFromStatName(makeStatName("foo"), Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(UngroupedStatsRequestTest, OneHistogram) {
  store_.rootScope()->histogramFromStatName(makeStatName("foo"),
                                            Stats::Histogram::Unit::Milliseconds);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Histograms)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(UngroupedStatsRequestTest, OneTextReadout) {
  store_.rootScope()->textReadoutFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::TextReadouts)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(UngroupedStatsRequestTest, OneScope) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(UngroupedStatsRequestTest, ManyStatsSmallChunkSize) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.rootScope()->counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<UngroupedStatsRequest> request =
      makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request));
}

TEST_F(UngroupedStatsRequestTest, ManyStatsSmallChunkSizeNoDrain) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.rootScope()->counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<UngroupedStatsRequest> request =
      makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request, false));
}

TEST_F(UngroupedStatsRequestTest, OneStatUsedOnly) {
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(0, iterateChunks(*makeRequest(true, StatsFormat::Text, StatsType::All)));
}

TEST_F(UngroupedStatsRequestTest, OneStatJson) {
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_THAT(response(*makeRequest(false, StatsFormat::Json, StatsType::All)),
              testing::StartsWith("{"));
}

} // namespace Server
} // namespace Envoy
