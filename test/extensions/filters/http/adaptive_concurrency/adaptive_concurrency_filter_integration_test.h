#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

const std::string ADAPTIVE_CONCURRENCY_CONFIG =
    R"EOF(
name: envoy.filters.http.adaptive_concurrency
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.adaptive_concurrency.v2alpha.AdaptiveConcurrency
  gradient_controller_config:
    sample_aggregate_percentile:
      value: 50
    concurrency_limit_params:
      concurrency_update_interval: 0.1s
    min_rtt_calc_params:
      interval: 30s
      request_count: 50
      min_concurrency: 1
)EOF";

const std::string CONCURRENCY_LIMIT_GAUGE_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.concurrency_limit";
const std::string REQUEST_BLOCK_COUNTER_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.rq_blocked";
const std::string MIN_RTT_GAUGE_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.min_rtt_msecs";

class AdaptiveConcurrencyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest {
public:
  AdaptiveConcurrencyIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void customInit() {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    config_helper_.addFilter(ADAPTIVE_CONCURRENCY_CONFIG);
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  void TearDown() override {
    HttpIntegrationTest::cleanupUpstreamAndDownstream();
    codec_client_->close();
    codec_client_.reset();
  }

protected:
  // Send some number of requests with 'delay_ms' specifying the amount of time the fault filter
  // will delay them.
  void sendRequests(uint32_t request_count, uint32_t num_forwarded);

  // Waits for a specified duration and then responds to all queued up requests in a FIFO manner.
  // Asserts that the expected number of requests are forwarded through the filter. The oldest
  // requests are the forwarded requests.
  //
  // Note: For interleaved forwarded/blocked requests, use respondToRequest() directly.
  void respondToAllRequests(uint32_t forwarded_count, std::chrono::milliseconds latency);

  // Responds to a single request in a FIFO manner. Asserts the forwarding expectation.
  void respondToRequest(bool expect_forwarded);

  void verifyResponseForwarded(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  void verifyResponseBlocked(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  }

  std::deque<IntegrationStreamDecoderPtr> responses_;
  std::deque<FakeStreamPtr> upstream_requests_;
  std::deque<FakeHttpConnectionPtr> upstream_connections_;
};

} // namespace Envoy
