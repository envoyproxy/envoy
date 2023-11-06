#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/type/http.pb.h"

#include "test/integration/http_protocol_integration.h"

#include "fake_upstream.h"
#include "http_integration.h"

namespace Envoy {
namespace {

class RetryBudgetIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { initialize(100.0); }

  void initialize(double budget_percent) {
    config_helper_.addConfigModifier(
        [budget_percent](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* retry_budget = bootstrap.mutable_static_resources()
                                   ->mutable_clusters(0)
                                   ->mutable_circuit_breakers()
                                   ->add_thresholds()
                                   ->mutable_retry_budget();
          retry_budget->mutable_min_retry_concurrency()->set_value(1);
          retry_budget->mutable_budget_percent()->set_value(budget_percent);
        });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* retry_policy =
              hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
          retry_policy->set_retry_on("5xx");
          retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_seconds(100000);
          retry_policy->mutable_retry_back_off()->mutable_max_interval()->set_seconds(100000);
        });

    HttpProtocolIntegrationTest::initialize();
  }

  void waitForUpstreamRequest() {
    if (free_upstream_conn_ == nullptr) {
      waitForNextUpstreamConnection({0}, TestUtility::DefaultTimeout, free_upstream_conn_);
    }

    if (upstream_request_ != nullptr) {
      unfinished_upstream_requests_.push_back(std::move(upstream_request_));
      upstream_request_ = nullptr;
    }

    AssertionResult result = free_upstream_conn_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      // Only one stream per connection for h1
      busy_upstream_conns_.push_back(std::move(free_upstream_conn_));
      free_upstream_conn_ = nullptr;
    }
  }

  void markUpstreamRequestComplete(bool expect_conn_closed = false) {
    EXPECT_TRUE(upstream_request_->complete());
    if (free_upstream_conn_ == nullptr) {
      ASSERT_TRUE(!busy_upstream_conns_.empty());
      free_upstream_conn_ = std::move(busy_upstream_conns_.back());
      busy_upstream_conns_.pop_back();
    }
    upstream_request_.reset();
    if (expect_conn_closed) {
      unusable_upstream_conns_.push_back(std::move(free_upstream_conn_));
      free_upstream_conn_.reset();
    }
  }

  void createActiveRequest() {
    auto codec_client = makeHttpConnection(lookupPort("http"));
    auto response = codec_client->makeHeaderOnlyRequest(default_request_headers_);

    waitForUpstreamRequest();
    codec_clients_.push_back(std::move(codec_client));
    active_request_responses_.push_back(std::move(response));
  }

  void createRetryingRequest(bool expect_response = false, bool include_response_body = false) {
    auto codec_client = makeHttpConnection(lookupPort("http"));
    auto response = codec_client->makeHeaderOnlyRequest(default_request_headers_);

    waitForUpstreamRequest();
    upstream_request_->encodeHeaders(error_response_headers_, !include_response_body);
    if (include_response_body) {
      upstream_request_->encodeData("response body", true);
    }
    bool expect_conn_closed = include_response_body && upstreamProtocol() == Http::CodecType::HTTP1;
    markUpstreamRequestComplete(expect_conn_closed);
    codec_clients_.push_back(std::move(codec_client));

    if (expect_response) {
      ASSERT_TRUE(response->waitForEndStream());
    } else {
      unfinished_responses_.push_back(std::move(response));
    }
  }

  void TearDown() override {
    for (auto& upstream_request : unfinished_upstream_requests_) {
      upstream_request->encodeHeaders(default_response_headers_, true);
      ASSERT_TRUE(upstream_request->complete());
    }
    for (auto& response : active_request_responses_) {
      ASSERT_TRUE(response->waitForEndStream());
    }
    for (auto& conn : busy_upstream_conns_) {
      if (conn != nullptr) {
        AssertionResult result = conn->close();
        RELEASE_ASSERT(result, result.message());
        result = conn->waitForDisconnect();
        RELEASE_ASSERT(result, result.message());
        conn.reset();
      }
    }
    for (auto& conn : unusable_upstream_conns_) {
      if (conn != nullptr) {
        AssertionResult result = conn->close();
        RELEASE_ASSERT(result, result.message());
        result = conn->waitForDisconnect();
        RELEASE_ASSERT(result, result.message());
        conn.reset();
      }
    }
    for (auto& client : codec_clients_) {
      if (client->rawConnection().state() == Envoy::Network::Connection::State::Open) {
        // Manually abort ongoing requests (retries still in backoff)
        client->rawConnection().close(Envoy::Network::ConnectionCloseType::Abort);
      }
      client->close();
    }
    unfinished_upstream_requests_.clear();
    active_request_responses_.clear();
    busy_upstream_conns_.clear();
    codec_clients_.clear();
  }

protected:
  std::vector<Envoy::IntegrationCodecClientPtr> codec_clients_;
  std::vector<Envoy::IntegrationStreamDecoderPtr> unfinished_responses_;
  std::vector<Envoy::IntegrationStreamDecoderPtr> active_request_responses_;
  std::vector<Envoy::FakeStreamPtr> unfinished_upstream_requests_;
  Http::TestResponseHeaderMapImpl error_response_headers_{{":status", "500"}};

  std::vector<Envoy::FakeHttpConnectionPtr> busy_upstream_conns_;
  Envoy::FakeHttpConnectionPtr free_upstream_conn_;
  std::vector<Envoy::FakeHttpConnectionPtr> unusable_upstream_conns_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, RetryBudgetIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(RetryBudgetIntegrationTest, NoOpRetryBudget) {
  initialize(100.0);

  // Send a few requests that all fail and ensure that they all retry
  uint64_t num_requests = 10;

  for (uint64_t i = 0; i < num_requests; i++) {
    createRetryingRequest();

    // All requests should be currently sitting in exponential retry backoff
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value(), i + 1);
    EXPECT_EQ(
        test_server_->counter("cluster.cluster_0.upstream_rq_retry_backoff_exponential")->value(),
        i + 1);
    EXPECT_EQ(test_server_->gauge("cluster.cluster_0.upstream_rq_retry_active")->value(), i + 1);
    EXPECT_EQ(test_server_->gauge("cluster.cluster_0.upstream_rq_retry_backoff_exponential_active")
                  ->value(),
              i + 1);
    // No retries were blocked due to circuit breaker limits
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 0);
  }
}

TEST_P(RetryBudgetIntegrationTest, RetryBudgetRespectBudgetPercentage) {
  initialize(50.0);

  // Create a few active requests to set the limits
  uint64_t num_active = 10;
  for (uint64_t i = 0; i < num_active; i++) {
    createActiveRequest();
  }

  EXPECT_EQ(test_server_->gauge("cluster.cluster_0.upstream_rq_active")->value(), num_active);

  // Run more requests that get immediate retriable error responses from the server
  // Check that a suitable proportion of the active request count are allowed to enter (infinite)
  // backoff The rest should return immediately without retrying

  // N - 1 where N is smallest integer such that floor((20 + N + k) * 20.0 / 100.0) < N
  // and k = 1 if the upstream protocol is HTTP2 otherwise k = 0
  uint64_t expected_num_retry_backoff = 10 + (upstreamProtocol() == Http::CodecType::HTTP2 ? 0 : 1);
  // once one overflows, every request after that is sure to overflow as well
  uint64_t expected_num_retry_overflow = 1;

  for (uint64_t i = 0; i < expected_num_retry_backoff + expected_num_retry_overflow; i++) {
    createRetryingRequest(i >= expected_num_retry_backoff);

    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", num_active);
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active",
                                 std::min<uint64_t>(i + 1, expected_num_retry_backoff));
    EXPECT_EQ(test_server_->gauge("cluster.cluster_0.upstream_rq_retry_backoff_exponential_active")
                  ->value(),
              std::min<uint64_t>(i + 1, expected_num_retry_backoff));
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value(),
              std::min<uint64_t>(i + 1, expected_num_retry_backoff));
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(),
              i < expected_num_retry_backoff ? 0 : i + 1 - expected_num_retry_backoff);
  }
}

TEST_P(RetryBudgetIntegrationTest, RetryBudgetRespectBudgetPercentageWithResponseBody) {
  initialize(50.0);

  // Create an active requests to set the limits
  createActiveRequest();

  EXPECT_EQ(test_server_->gauge("cluster.cluster_0.upstream_rq_active")->value(), 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);

  // First retry will always be allowed to go through
  createRetryingRequest(false, true);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 1);

  // Active=3, so max=floor(3*0.5)=1
  // Second request should on paper be blocked, but due to codec inconsistencies
  // around when a stream is considered "destroyed" for header-only requests,
  // there's a +1 buffer room in the budget calculations. For responses with bodies
  // the stream will always still be active on retry determination,
  // so we always get a +1 to the active count compared to the theoretically correct value.
  // That gives us max=floor(4*0.5)=2
  createRetryingRequest(false, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 2);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 0);

  // Now active=4, so max=floor((4 + 1)*0.5)=2
  createRetryingRequest(true, true);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 1);
}

TEST_P(RetryBudgetIntegrationTest, RetryBudgetRecoupBudgetOnRequestAbandoned) {
  initialize(0.0);

  // Use our 1 retry slot for a retry in backoff
  createRetryingRequest(false);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 1);

  // Another retry will overflow
  createRetryingRequest(true);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 1);

  // Now cancel the downstream request for the retry in backoff
  TearDown();
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 0);

  // Next retriable request should get the freed retry slot
  createRetryingRequest(false);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_retry_active", 1);
}
} // namespace
} // namespace Envoy
