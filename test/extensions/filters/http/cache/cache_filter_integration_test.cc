#include "test/integration/http_protocol_integration.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
public:
  void TearDown() override {
    cleanupUpstreamAndDownstream();
    HttpProtocolIntegrationTest::TearDown();
  }

  void initializeFilter(const std::string& config) {
    config_helper_.addFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  const std::string default_config{R"EOF(
    name: envoy.cache
    typed_config:
        "@type": type.googleapis.com/envoy.config.filter.http.cache.v2alpha.Cache
        name: SimpleHttpCache
    )EOF"};
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
};

INSTANTIATE_TEST_SUITE_P(Protocols, CacheIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CacheIntegrationTest, MissInsertHit) {
  // Set system time to cause Envoy's cached formatted time to match time on this thread.
  simTime().setSystemTime(std::chrono::hours(1));
  initializeFilter(default_config);

  // Include test name and params in URL to make each test's requests unique.
  const Http::TestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", absl::StrCat("/", protocolTestParamsToString({GetParam(), 0}))},
      {":scheme", "http"},
      {":authority", "MissInsertHit"}};
  Http::TestHeaderMapImpl response_headers = {{":status", "200"},
                                              {"date", formatter_.now(simTime())},
                                              {"cache-control", "public,max-age=3600"},
                                              {"content-length", "42"}};

  // Send first request, and get response from upstream.
  {
    IntegrationStreamDecoderPtr request = codec_client_->makeHeaderOnlyRequest(request_headers);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(response_headers, /*end_stream=*/false);
    // send 42 'a's
    upstream_request_->encodeData(42, true);
    // Wait for the response to be read by the codec client.
    request->waitForEndStream();
    EXPECT_TRUE(request->complete());
    EXPECT_THAT(request->headers(), IsSupersetOfHeaders(response_headers));
    EXPECT_EQ(request->headers().Age(), nullptr);
    EXPECT_EQ(request->body(), std::string(42, 'a'));
  }

  // Send second resquest, and get response from cache.
  IntegrationStreamDecoderPtr request = codec_client_->makeHeaderOnlyRequest(request_headers);
  request->waitForEndStream();
  EXPECT_TRUE(request->complete());
  EXPECT_THAT(request->headers(), IsSupersetOfHeaders(response_headers));
  EXPECT_EQ(request->body(), std::string(42, 'a'));
  EXPECT_NE(request->headers().Age(), nullptr);
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
