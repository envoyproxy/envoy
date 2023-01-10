#include "source/common/stats/custom_stat_namespaces_impl.h"

#include "test/server/admin/stats_request_test_base.h"

namespace Envoy {
namespace Server {

class PrometheusStatsRequestTest : public StatsRequestTestBase<PrometheusStatsRequest> {
protected:
  std::unique_ptr<PrometheusStatsRequest> makeRequest(bool used_only, bool text_readouts = false) {
    StatsParams params;
    params.used_only_ = used_only;
    params.prometheus_text_readouts_ = text_readouts;
    return std::make_unique<PrometheusStatsRequest>(store_, params, custom_namespaces_);
  }

  Stats::CustomStatNamespacesImpl custom_namespaces_;
};

TEST_F(PrometheusStatsRequestTest, Empty) {
  EXPECT_EQ(0, iterateChunks(*makeRequest(false)));
}

TEST_F(PrometheusStatsRequestTest, OneCounter) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  Stats::Counter& c1 =
      store_.counterFromStatNameWithTags(makeStatName("cluster.upstream.cx.total"), c1Tags);
  c1.add(10);

  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(PrometheusStatsRequestTest, OneGauge) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.gaugeFromStatNameWithTags(makeStatName("foo"), c1Tags, Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(PrometheusStatsRequestTest, OneHistogram) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.histogramFromStatNameWithTags(makeStatName("foo"), c1Tags, Stats::Histogram::Unit::Milliseconds);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false)));
}

TEST_F(PrometheusStatsRequestTest, OneTextReadout) {
  Stats::StatNameTagVector c1Tags{{makeStatName("cluster"), makeStatName("c1")}};
  store_.textReadoutFromStatNameWithTags(makeStatName("foo"), c1Tags);
  // text readouts are not included in the returned prometheus stats, unless specifically asked for via query param
  EXPECT_EQ(0, iterateChunks(*makeRequest(false)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, true)));
}

TEST_F(PrometheusStatsRequestTest, OneScope) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(false)));
 }

// example output:
// # TYPE envoy_foo6 counter
// envoy_foo6{cluster="c1"} 0
TEST_F(PrometheusStatsRequestTest, ManyStatsSmallChunkSize) {
  for (uint32_t i = 0; i < 10; ++i) {
    Stats::StatNameTagVector tags{{makeStatName("cluster"), makeStatName("c1")}};
    store_.counterFromStatNameWithTags(makeStatName(absl::StrCat("foo", i)), tags);
  }
  std::unique_ptr<PrometheusStatsRequest> request = makeRequest(false);
  request->setChunkSize(50);
  EXPECT_EQ(10, iterateChunks(*request));
}

TEST_F(PrometheusStatsRequestTest, ManyStatsSmallChunkSizeNoDrain) {
  for (uint32_t i = 0; i < 10; ++i) {
    Stats::StatNameTagVector tags{{makeStatName("cluster"), makeStatName("c1")}};
    store_.counterFromStatNameWithTags(makeStatName(absl::StrCat("foo", i)), tags);
  }
  std::unique_ptr<PrometheusStatsRequest> request = makeRequest(false);
  request->setChunkSize(50);
  EXPECT_EQ(10, iterateChunks(*request, false));
}

TEST_F(PrometheusStatsRequestTest, OneStatUsedOnly) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(true)));
}

} // namespace Server
} // namespace Envoy
