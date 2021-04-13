#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class ReqRespSizeStatsIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  ReqRespSizeStatsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, Network::Address::IpVersion::v4) {}
};

TEST_F(ReqRespSizeStatsIntegrationTest, Basic) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.mutable_track_cluster_stats()->set_request_response_sizes(true);
  });
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/found"}, {":scheme", "http"}, {":authority", "foo.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0, 0,
                                                TestUtility::DefaultTimeout);
  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rq_headers_size");
  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rs_headers_size");
}

} // namespace Http
} // namespace Envoy
