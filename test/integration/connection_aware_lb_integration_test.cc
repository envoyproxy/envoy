#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "test/config/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using ::testing::Eq;
using ::testing::Ge;

// Base fixture: a single static cluster (cluster_0) with connection-aware LB.
class ConnectionAwareLbIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  ConnectionAwareLbIntegrationTest() { async_lb_ = false; }

  void enableConnectionAwareLb() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_connection_aware_load_balancing();
    });
  }

  void setFloor(uint32_t floor) {
    config_helper_.addConfigModifier([floor](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_preconnect_policy()
          ->mutable_eager_preconnect_floor()
          ->set_value(floor);
    });
  }

  void setMaxConnections(uint32_t max_connections) {
    config_helper_.addConfigModifier(
        [max_connections](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* threshold = bootstrap.mutable_static_resources()
                                ->mutable_clusters(0)
                                ->mutable_circuit_breakers()
                                ->add_thresholds();
          threshold->set_priority(envoy::config::core::v3::DEFAULT);
          threshold->mutable_max_connections()->set_value(max_connections);
        });
  }

  void setHostCount(uint32_t host_count) {
    setUpstreamCount(host_count);
    config_helper_.addConfigModifier(
        [host_count](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* endpoints = bootstrap.mutable_static_resources()
                                ->mutable_clusters(0)
                                ->mutable_load_assignment()
                                ->mutable_endpoints(0);
          const auto seed = endpoints->lb_endpoints(0);
          for (uint32_t i = 1; i < host_count; ++i) {
            *endpoints->add_lb_endpoints() = seed;
          }
        });
  }

  void connect() { codec_client_ = makeHttpConnection(lookupPort("http")); }

  void sendRequestExpecting(absl::string_view expected_status) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(expected_status, response->headers().getStatusValue());
  }

  std::string clusterStat(absl::string_view name) {
    return absl::StrCat("cluster.cluster_0.", name);
  }
  uint64_t counterValue(absl::string_view name) {
    auto counter = test_server_->counter(clusterStat(name));
    return counter == nullptr ? 0 : counter->value();
  }
  uint64_t warmSelected() { return counterValue("upstream_cx_lb_selected_warm"); }
  uint64_t coldSelected() { return counterValue("upstream_cx_lb_selected_cold"); }
  uint64_t primingStarted() { return counterValue("upstream_cx_preconnect_started"); }
  uint64_t primingBlocked() { return counterValue("upstream_cx_preconnect_blocked"); }
};

// ---------------------------------------------------------------------------
// Selection is classified as cold or warm, and prefers warm hosts.
// ---------------------------------------------------------------------------

INSTANTIATE_TEST_SUITE_P(Protocols, ConnectionAwareLbIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Plain load balancing.
TEST_P(ConnectionAwareLbIntegrationTest, Disabled) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  initialize();

  connect();
  sendRequestExpecting("200");
  test_server_->waitForCounter(clusterStat("upstream_rq_total"), Eq(1));

  EXPECT_EQ(0, warmSelected());
  EXPECT_EQ(0, coldSelected());

  cleanupUpstreamAndDownstream();
}

// The first pick against an unprimed cluster is cold.
TEST_P(ConnectionAwareLbIntegrationTest, ColdWhenUnprimed) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  initialize();

  connect();
  sendRequestExpecting("200");

  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_cold"), Ge(1));
  EXPECT_EQ(0, warmSelected());
  EXPECT_EQ(0, primingStarted()) << "no floor means a rejected cold host is not primed";

  cleanupUpstreamAndDownstream();
}

// Once a host has a ready connection, selection prefers it.
TEST_P(ConnectionAwareLbIntegrationTest, PrefersWarm) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  setHostCount(2);
  initialize();

  // The first pick is cold, but creates a connection on the host that served it.
  constexpr uint32_t kRequests = 5;
  connect();
  sendRequestExpecting("200");
  const uint64_t warm_before = warmSelected();

  for (uint32_t i = 1; i < kRequests; ++i) {
    sendRequestExpecting("200");
  }

  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_warm"), Ge(warm_before + 1));
  test_server_->waitForCounter(clusterStat("upstream_rq_total"), Eq(kRequests));
  EXPECT_GE(coldSelected(), 1);
  EXPECT_EQ(kRequests, warmSelected() + coldSelected());
  EXPECT_EQ(0, primingStarted()) << "warming came from served requests, not priming";

  cleanupUpstreamAndDownstream();
}

// An unhealthy host is excluded from selection.
TEST_P(ConnectionAwareLbIntegrationTest, ExcludesUnhealthy) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);
    cluster->mutable_load_assignment()
        ->mutable_endpoints(0)
        ->mutable_lb_endpoints(0)
        ->set_health_status(envoy::config::core::v3::UNHEALTHY);
  });
  initialize();

  test_server_->waitForGauge(clusterStat("membership_healthy"), Eq(0));

  connect();
  sendRequestExpecting("503");

  EXPECT_EQ(0, warmSelected());
  EXPECT_EQ(0, coldSelected());

  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Connection manipulation.
// ---------------------------------------------------------------------------

class ConnectionAwareLbConnCloseIntegrationTest : public ConnectionAwareLbIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ConnectionAwareLbConnCloseIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Readiness is re-evaluated per selection.
TEST_P(ConnectionAwareLbConnCloseIntegrationTest, ColdAfterClose) {
  concurrency_ = 1;
  enableConnectionAwareLb();
  setMaxConnections(1);
  initialize();

  // Warm the host by serving one request, leaving its connection idle and ready.
  connect();
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  FakeHttpConnectionPtr upstream_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_conn));
  FakeStreamPtr upstream_stream1;
  ASSERT_TRUE(upstream_conn->waitForNewStream(*dispatcher_, upstream_stream1));
  ASSERT_TRUE(upstream_stream1->waitForHeadersComplete());
  upstream_stream1->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());

  // Close the connection from the upstream side; the host now has no ready connection.
  ASSERT_TRUE(upstream_conn->close());
  ASSERT_TRUE(upstream_conn->waitForDisconnect());
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(0));
  const uint64_t cold_before = coldSelected();

  // The host is cold again, so the next pick is classified cold and served on a fresh connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  FakeHttpConnectionPtr upstream_conn2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_conn2));
  FakeStreamPtr upstream_stream2;
  ASSERT_TRUE(upstream_conn2->waitForNewStream(*dispatcher_, upstream_stream2));
  ASSERT_TRUE(upstream_stream2->waitForHeadersComplete());
  upstream_stream2->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_cold"), Ge(cold_before + 1));
  EXPECT_EQ(0, warmSelected());
  EXPECT_EQ(0, primingStarted()) << "no floor means the re-cooled host is not primed";

  cleanupUpstreamAndDownstream();
}

// Readiness needs spare stream capacity, not just an active connection.
TEST_P(ConnectionAwareLbConnCloseIntegrationTest, SaturatedIsCold) {
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  concurrency_ = 1;
  setUpstreamCount(1);
  enableConnectionAwareLb();
  setMaxConnections(1);
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::setHttp2WithMaxConcurrentStreams(
        *bootstrap.mutable_static_resources()->mutable_clusters(0), 1);
  });
  initialize();

  // Request 1 occupies the only permitted connection's only stream.
  connect();
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  FakeHttpConnectionPtr upstream_conn;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_conn));
  FakeStreamPtr upstream_stream1;
  ASSERT_TRUE(upstream_conn->waitForNewStream(*dispatcher_, upstream_stream1));
  ASSERT_TRUE(upstream_stream1->waitForHeadersComplete());
  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_cold"), Ge(1));

  // Request 2 is on a separate downstream connection, so the host is saturated and the pick is
  // cold.
  auto codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client2->makeHeaderOnlyRequest(default_request_headers_);
  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_cold"), Ge(2));
  EXPECT_EQ(0, warmSelected());
  EXPECT_EQ(0, primingStarted()) << "no floor means the saturated host is not primed";
  EXPECT_EQ(0, primingBlocked());

  // Release request 1; the freed connection then serves request 2.
  upstream_stream1->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  FakeStreamPtr upstream_stream2;
  ASSERT_TRUE(upstream_conn->waitForNewStream(*dispatcher_, upstream_stream2));
  ASSERT_TRUE(upstream_stream2->waitForHeadersComplete());
  upstream_stream2->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  codec_client2->close();
  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Multiple workers.
// ---------------------------------------------------------------------------

class ConnectionAwareLbMultiWorkerIntegrationTest : public ConnectionAwareLbIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ConnectionAwareLbMultiWorkerIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Readiness is per worker: a connection warm on one worker does not warm the host elsewhere.
TEST_P(ConnectionAwareLbMultiWorkerIntegrationTest, PerWorkerReadiness) {
  concurrency_ = 2;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_connection_balance_config()
        ->mutable_exact_balance();
  });
  initialize();

  // One downstream connection per worker.
  std::vector<IntegrationCodecClientPtr> clients;
  for (uint32_t i = 0; i < concurrency_; ++i) {
    auto client = makeHttpConnection(lookupPort("http"));
    auto response = client->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    clients.push_back(std::move(client));
  }

  // Each worker classifies its own first request as cold.
  test_server_->waitForCounter(clusterStat("upstream_cx_lb_selected_cold"), Ge(concurrency_));
  EXPECT_EQ(0, primingStarted()) << "no cold hosts are primed";

  for (auto& client : clients) {
    client->close();
  }
}

// ---------------------------------------------------------------------------
// Eager preconnects enabled.
// ---------------------------------------------------------------------------

class ConnectionAwareLbPrimingIntegrationTest : public ConnectionAwareLbIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ConnectionAwareLbPrimingIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ConnectionAwareLbPrimingIntegrationTest, PrimesCold) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  setFloor(2);
  setHostCount(2);
  initialize();

  constexpr uint32_t kRequests = 5;
  connect();
  for (uint32_t i = 0; i < kRequests; ++i) {
    sendRequestExpecting("200");
  }

  // The host that serves no request is warmed by eager preconnects, counted at the pool layer.
  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_started"), Ge(1));
  test_server_->waitForCounter(clusterStat("upstream_rq_total"), Eq(kRequests));
  EXPECT_GE(warmSelected(), 1) << "later requests land on warmed hosts";
  EXPECT_EQ(kRequests, warmSelected() + coldSelected());
  EXPECT_EQ(0, primingBlocked());

  cleanupUpstreamAndDownstream();
}

TEST_P(ConnectionAwareLbPrimingIntegrationTest, PrimesBounded) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  enableConnectionAwareLb();
  setFloor(1);
  setHostCount(4);
  initialize();

  connect();
  sendRequestExpecting("200");
  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_started"), Ge(3));

  // Additional requests do not incur additional preconnects.
  constexpr uint32_t kRequests = 12;
  for (uint32_t i = 1; i < kRequests; ++i) {
    sendRequestExpecting("200");
  }
  test_server_->waitForCounter(clusterStat("upstream_rq_total"), Eq(kRequests));

  EXPECT_LE(primingStarted(), 4) << "each of the four hosts is preconnected at most once";
  EXPECT_EQ(0, primingBlocked());

  cleanupUpstreamAndDownstream();
}

TEST_P(ConnectionAwareLbPrimingIntegrationTest, StopsForUnreachable) {
  concurrency_ = 1;
  enableConnectionAwareLb();
  setFloor(2);
  // Point the single host at a dead port so every connect is refused before the handshake.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_load_assignment()
        ->mutable_endpoints(0)
        ->mutable_lb_endpoints(0)
        ->mutable_endpoint()
        ->mutable_address()
        ->mutable_socket_address()
        ->set_port_value(1);
  });
  config_helper_.skipPortUsageValidation();
  initialize();

  // Each request selects the cold host, fails to connect, and returns 503.
  constexpr uint32_t kRequests = 15;
  for (uint32_t i = 0; i < kRequests; ++i) {
    auto client = makeHttpConnection(lookupPort("http"));
    auto response = client->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    client->close();
  }

  // Eager preconnects stop once the host latches.
  EXPECT_LT(primingStarted(), 10);
  EXPECT_GE(coldSelected(), 1);
  EXPECT_EQ(0, primingBlocked());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
