#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

const std::string ADAPTIVE_CONCURRENCY_CONFIG =
    R"EOF(
name: envoy.filters.http.adaptive_concurrency
config:
  gradient_controller_config:
    sample_aggregate_percentile:
      value: 50
    concurrency_limit_params:
      concurrency_update_interval: 0.1s
    min_rtt_calc_params:
      interval: 30s
      request_count: 50
)EOF";

const std::string FAULT_FILTER_CONFIG =
    R"EOF(
name: envoy.fault
config:
    delay:
        header_delay: {}
        percentage:
            numerator: 100
            denominator: HUNDRED
  )EOF";

const std::string CONCURRENCY_LIMIT_GAUGE_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.concurrency_limit";
const std::string REQUEST_BLOCK_COUNTER_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.rq_blocked";
const std::string MIN_RTT_GAUGE_NAME =
    "http.config_test.adaptive_concurrency.gradient_controller.min_rtt_msecs";

// The default delay introduced to each sent request.
const uint32_t DEFAULT_REQUEST_DELAY_MS = 50;

class AdaptiveConcurrencyIntegrationTest
    : public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdaptiveConcurrencyIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void initializeFilter() {
    // We use the fault filter (for delays) after the adaptive concurrency filter to introduce a
    // "service latency" to the test. This way, time is moved forward with each response.
    config_helper_.addFilter(ADAPTIVE_CONCURRENCY_CONFIG);
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  void TearDown() override {}

  void initialize() override {
    initializeFilter();
    HttpIntegrationTest::initialize();
  }

protected:
  // Send some number of requests with 'delay_ms' specifying the amount of time the fault filter
  // will delay them.
  void sendRequests(const uint32_t request_count, const uint32_t num_forwarded);

  // Responds to all queued up requests in a FIFO manner and asserts that the exact number specified
  // are forwarded.  Assumes the oldest requests are the forwarded requests.
  //
  // Note: For interleaved forwarded/blocked requests, use respondToRequest() directly.
  void respondToAllRequests(const int forwarded_count);

  // Responds to a single request in a FIFO manner. Asserts the forwarding expectation.
  void respondToRequest(const bool expect_forwarded);

  // Inflates the concurrency limit to >= the specified value. Returns the concurrency limit.
  uint32_t inflateConcurrencyLimit(const uint64_t limit_lower_bound);

  // Deflates the concurrency limit to <= the specified value.
  void deflateConcurrencyLimit(const uint64_t limit_upper_bound);

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
