#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

const std::string kAdaptiveConcurrencyFilterConfig =
    R"EOF(
name: envoy.filters.http.adaptive_concurrency
config:
  gradient_controller_config:
    sample_aggregate_percentile:
      value: 50
    concurrency_limit_params:
      concurrency_update_interval:
        nanos: 100000000 # 100ms
    min_rtt_calc_params:
      interval:
        seconds: 30
      request_count: 50
)EOF";

const std::string kFaultFilterConfig =
    R"EOF(
name: envoy.fault
config:
    delay:
        fixed_delay:
            nanos: 5000000 # 5ms
        percentage:
            numerator: 100
            denominator: HUNDRED
  )EOF";

const std::string kConcurrencyLimitGaugeName =
    "http.config_test.adaptive_concurrency.gradient_controller.concurrency_limit";
const std::string kRequestBlockCounterName =
    "http.config_test.adaptive_concurrency.gradient_controller.rq_blocked";

class AdaptiveConcurrencyIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdaptiveConcurrencyIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void initializeFilter() {
    // We use the fault filter (for delays) after the adaptive concurrency filter to introduce a
    // "service latency" to the test. This way, time is moved forward with each response.
    config_helper_.addFilter(kFaultFilterConfig);
    config_helper_.addFilter(kAdaptiveConcurrencyFilterConfig);
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    autonomous_upstream_ = true;
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  void TearDown() override {}

  void initialize() override {
    initializeFilter();
    HttpIntegrationTest::initialize();
  }

protected:
  void sendRequests(const int request_count);

  // Responds to all queued up requests and asserts that the exact number specified are forwarded.
  void respondToAllRequests(const int num_forwarded);

  // Responds to a single request in a FIFO manner.
  IntegrationStreamDecoderPtr respondToRequest();

  // Inflates the concurrency limit to >= the specified value.
  void inflateConcurrencyLimit(const uint64_t limit_lower_bound);

  void verifyResponseForwarded(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  void verifyResponseBlocked(IntegrationStreamDecoderPtr response) {
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  }

private:
  std::queue<IntegrationStreamDecoderPtr> response_q_;
};

} // namespace Envoy
