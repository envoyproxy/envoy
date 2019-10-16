#include "test/integration/http_protocol_integration.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace {

class ConnectionDurationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          if (enable_connection_duration_timeout_) {
            hcm.mutable_max_connection_duration()->set_seconds(0);
            hcm.mutable_max_connection_duration()->set_nanos(MaxConnectionDurationMs * 1000 * 1000);
          }

          // For validating encode100ContinueHeaders() timer kick.
          hcm.set_proxy_100_continue(true);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  // TODO(htuch): This might require scaling for TSAN/ASAN/Valgrind/etc. Bump if
  // this is the cause of flakes.
  static constexpr uint64_t MaxConnectionDurationMs = 400;

  bool enable_connection_duration_timeout_{false};
  DangerousDeprecatedTestTime test_time_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, ConnectionDurationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Test connection is closed after single request processed.
TEST_P(ConnectionDurationIntegrationTest, TimeoutBasic) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}

// Test inflight request is processed correctly when timeout fires during request processing.
TEST_P(ConnectionDurationIntegrationTest, InflightRequest) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // block and wait for counter to increase
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);

  // ensure request processed correctly
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
}
// Test connection is closed if no http requests were processed
TEST_P(ConnectionDurationIntegrationTest, TimeoutNoHttpRequest) {
  enable_connection_duration_timeout_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}
} // namespace
} // namespace Envoy
