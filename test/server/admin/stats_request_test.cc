#include "test/server/admin/stats_request_test_base.h"

namespace Envoy {
namespace Server {

class StatsRequestTest : public StatsRequestTestBase<StatsRequest> {
protected:
  std::unique_ptr<StatsRequest> makeRequest(bool used_only, StatsFormat format, StatsType type) {
    StatsParams params;
    params.used_only_ = used_only;
    params.type_ = type;
    params.format_ = format;
    return std::make_unique<StatsRequest>(store_, params);
  }
};

TEST_F(StatsRequestTest, Empty) {
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, OneCounter) {
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
}

TEST_F(StatsRequestTest, OneGauge) {
  store_.rootScope()->gaugeFromStatName(makeStatName("foo"), Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneHistogram) {
  store_.rootScope()->histogramFromStatName(makeStatName("foo"),
                                            Stats::Histogram::Unit::Milliseconds);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Histograms)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneTextReadout) {
  store_.rootScope()->textReadoutFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::TextReadouts)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneScope) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, ManyStatsSmallChunkSize) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.rootScope()->counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<StatsRequest> request = makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request));
}

TEST_F(StatsRequestTest, ManyStatsSmallChunkSizeNoDrain) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.rootScope()->counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<StatsRequest> request = makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request, false));
}

TEST_F(StatsRequestTest, OneStatUsedOnly) {
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(0, iterateChunks(*makeRequest(true, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, OneStatJson) {
<<<<<<< Updated upstream
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_THAT(response(*makeRequest(false, StatsFormat::Json, StatsType::All)), StartsWith("{"));
}

TEST_F(StatsRequestTest, OneStatPrometheus) {
  // Currently the rendering infrastructure does not support Prometheus -- that
  // gets rendered using a different code-path. This will be fixed at some
  // point, to make Prometheus consume less resource, and when that occurs this
  // test can exercise that.
  store_.rootScope()->counterFromStatName(makeStatName("foo"));
  EXPECT_ENVOY_BUG(iterateChunks(*makeRequest(false, StatsFormat::Prometheus, StatsType::All), true,
                                 Http::Code::BadRequest),
                   "reached Prometheus case in switch unexpectedly");
=======
  store_.counterFromStatName(makeStatName("foo"));
  EXPECT_THAT(response(*makeRequest(false, StatsFormat::Json, StatsType::All)), testing::StartsWith("{"));
>>>>>>> Stashed changes
}

} // namespace Server
} // namespace Envoy
