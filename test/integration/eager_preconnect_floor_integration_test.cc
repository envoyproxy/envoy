#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "test/config/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/base_overload_integration_test.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;

// Base fixture: a single static cluster (cluster_0) over the full protocol matrix.
class EagerPreconnectFloorIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  // Sets the eager preconnect floor on cluster_0.
  void setMinConnections(uint32_t min_connections) {
    config_helper_.addConfigModifier(
        [min_connections](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_preconnect_policy()->mutable_eager_preconnect_floor()->set_value(
              min_connections);
        });
  }

  // Sets the max_connections circuit-breaker threshold on cluster_0.
  void setMaxConnections(uint32_t max_connections) {
    config_helper_.addConfigModifier(
        [max_connections](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          auto* threshold = cluster->mutable_circuit_breakers()->add_thresholds();
          threshold->set_priority(envoy::config::core::v3::DEFAULT);
          threshold->mutable_max_connections()->set_value(max_connections);
        });
  }

  // Grows cluster_0 to `host_count` endpoints.
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

  // Serves one request to initialize preconnect floor.
  void serveOneRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    served_response_ = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  }

  std::string clusterStat(absl::string_view name) {
    return absl::StrCat("cluster.cluster_0.", name);
  }
  uint64_t counterValue(absl::string_view name) {
    auto counter = test_server_->counter(clusterStat(name));
    return counter == nullptr ? 0 : counter->value();
  }
  uint64_t gaugeValue(absl::string_view name) {
    auto gauge = test_server_->gauge(clusterStat(name));
    return gauge == nullptr ? 0 : gauge->value();
  }

  // Holds the open (unanswered) warm-up request stream so it is not reset before assertions run.
  IntegrationStreamDecoderPtr served_response_;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// ---------------------------------------------------------------------------
// Feature disabled: no eager preconnect floor is enforced, connections are created only on demand.
// ---------------------------------------------------------------------------

TEST_P(EagerPreconnectFloorIntegrationTest, NoFillWithoutFloor) {
  concurrency_ = 1;
  initialize();

  // Normal on-demand operation still works.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

TEST_P(EagerPreconnectFloorIntegrationTest, NoFillWhenRuntimeDisabled) {
  concurrency_ = 1;
  setMinConnections(3);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.eager_preconnect_floor", "false");
  initialize();

  // Normal on-demand operation still works.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Core floor fill: a used worker fills healthy hosts up to the floor.
// ---------------------------------------------------------------------------

TEST_P(EagerPreconnectFloorIntegrationTest, FillsFloorAfterFirstRequest) {
  concurrency_ = 1;
  setMinConnections(3);
  initialize();

  // Nothing is opened before any request.
  EXPECT_EQ(0, gaugeValue("upstream_cx_active"));

  serveOneRequest();

  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Ge(3));
  // The served host's floor (3) is filled synchronously by the demand path: newStream's
  // tryCreateNewConnections opens up to 3 connections in one burst, which here covers the whole
  // floor. Those ride the stream-serving path, not the anticipatory maybePreconnect path.
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

TEST_P(EagerPreconnectFloorIntegrationTest, FillsFloorAboveDemandBurstCap) {
  concurrency_ = 1;
  setMinConnections(5);
  initialize();

  serveOneRequest();

  // The floor (5) is reached: 3 from the demand burst plus 2 opened by the eager floor-fill path.
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Ge(5));
  EXPECT_EQ(2, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// Serving a single request fills connections for all current hosts, not just the one it lands on.
TEST_P(EagerPreconnectFloorIntegrationTest, FillsFloorForEachHost) {
  concurrency_ = 1;
  setHostCount(2);
  setMinConnections(2);
  initialize();

  serveOneRequest();

  // The host that receives no demand is filled entirely by the floor-fill path, so preconnect_started
  // counts at least its floor (2).
  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_started"), Ge(2));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// Multi-worker fixture. Runs on the non-HTTP/3 matrix because a QUIC downstream listener on an
// ephemeral port is rejected by the harness under concurrency > 1.
class EagerPreconnectFloorMultiWorkerIntegrationTest : public EagerPreconnectFloorIntegrationTest {
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorMultiWorkerIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Serving on several downstream connections warms multiple workers; each fills its own floor, so the
// aggregate grows past a single worker's floor.
TEST_P(EagerPreconnectFloorMultiWorkerIntegrationTest, FillsFloorPerServingWorker) {
  concurrency_ = 2;
  setMinConnections(3);
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_connection_balance_config()->mutable_exact_balance();
  });
  initialize();

  // Keep one open downstream connection per worker so exact balancing warms every worker.
  std::vector<IntegrationCodecClientPtr> clients;
  std::vector<IntegrationStreamDecoderPtr> responses;
  for (uint32_t i = 0; i < concurrency_; ++i) {
    clients.push_back(makeHttpConnection(lookupPort("http")));
    responses.push_back(clients.back()->makeHeaderOnlyRequest(default_request_headers_));
  }
  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
  }

  // Each worker maintains its own floor. One worker's floor is 3, so a total above 3 connections
  // proves a second worker independently filled its floor too.
  test_server_->waitForCounter(clusterStat("upstream_cx_total"), Ge(4));
  // Each worker's floor is filled synchronously by its own demand request,
  // so the eager floor-fill path opens nothing and blocks nothing.
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  for (auto& client : clients) {
    client->close();
  }
  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Steady-state maintenance: the floor is held as connections churn.
// ---------------------------------------------------------------------------

// Connection-manipulation fixture. Runs on the non-HTTP/3 matrix: these tests grab and close a raw
// upstream connection (waitForHttpConnection), which is only defined for HTTP1/HTTP2 upstreams.
class EagerPreconnectFloorConnCloseIntegrationTest : public EagerPreconnectFloorIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorConnCloseIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// When a connection closes below the floor, the pool refills back up to it.
TEST_P(EagerPreconnectFloorConnCloseIntegrationTest, RefillsFloorOnConnectionClose) {
  concurrency_ = 1;
  setMinConnections(2);
  setMaxConnections(2);
  initialize();

  serveOneRequest();
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(2));
  const uint64_t total_before = counterValue("upstream_cx_total");

  // Accept one connection on the upstream and force-close it, dropping below the floor.
  FakeHttpConnectionPtr connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, connection));
  ASSERT_TRUE(connection->close());
  ASSERT_TRUE(connection->waitForDisconnect());

  // The floor is refilled.
  test_server_->waitForCounter(clusterStat("upstream_cx_destroy"), Ge(1));
  test_server_->waitForCounter(clusterStat("upstream_cx_total"), Ge(total_before + 1));
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(2));
  // The refill after the close is the eager floor-fill path, so it counts as a started preconnect.
  EXPECT_GE(counterValue("upstream_cx_preconnect_started"), 1);
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Membership changes: hosts added/removed at runtime are filled/drained.
// ---------------------------------------------------------------------------

// EDS fixture: sources cluster_0's endpoints from a filesystem EDS subscription so hosts can be
// added and removed at runtime.
class EagerPreconnectFloorEdsIntegrationTest : public EagerPreconnectFloorIntegrationTest {
protected:
  // Switches cluster_0 to EDS, sets the floor (and optional budget), warms it empty, starts the
  // server, then adds the initial hosts. Uses up to 4 fake upstreams so hosts can be added later.
  void initializeEds(uint32_t min_connections, uint32_t initial_host_count,
                     std::optional<uint32_t> max_connections = std::nullopt) {
    setUpstreamCount(4);
    config_helper_.addConfigModifier([this, min_connections, max_connections](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->clear_load_assignment();
      cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds = cluster->mutable_eds_cluster_config();
      eds->mutable_eds_config()->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      eds->mutable_eds_config()->mutable_path_config_source()->set_path(eds_helper_.edsPath());
      cluster->mutable_preconnect_policy()->mutable_eager_preconnect_floor()->set_value(
          min_connections);
      if (max_connections.has_value()) {
        auto* threshold = cluster->mutable_circuit_breakers()->add_thresholds();
        threshold->set_priority(envoy::config::core::v3::DEFAULT);
        threshold->mutable_max_connections()->set_value(*max_connections);
      }
    });
    envoy::config::endpoint::v3::ClusterLoadAssignment empty;
    empty.set_cluster_name("cluster_0");
    eds_helper_.setEds({empty});
    initialize();
    test_server_->waitForGauge("cluster_manager.warming_clusters", Eq(0));
    if (initial_host_count > 0) {
      setEdsHosts(makeHostIndices(0, initial_host_count));
    }
  }

  std::vector<uint32_t> makeHostIndices(uint32_t begin, uint32_t count) {
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < count; ++i) {
      indices.push_back(begin + i);
    }
    return indices;
  }

  // Publishes an EDS assignment for cluster_0 pointing at the given upstreams.
  void setEdsHosts(const std::vector<uint32_t>& upstream_indices, bool await = true) {
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");
    auto* locality = cluster_load_assignment.add_endpoints();
    for (uint32_t index : upstream_indices) {
      setUpstreamAddress(index, *locality->add_lb_endpoints());
    }
    if (await) {
      eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
    } else {
      eds_helper_.setEds({cluster_load_assignment});
    }
  }

  EdsHelper eds_helper_;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorEdsIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Once the worker is warmed, a host added to a running cluster via EDS is filled to the floor.
TEST_P(EagerPreconnectFloorEdsIntegrationTest, FillsHostAddedViaEds) {
  concurrency_ = 1;
  initializeEds(/*min_connections=*/2, /*initial_host_count=*/1);

  // Serving a request lets the initial host reach its floor.
  serveOneRequest();
  test_server_->waitForCounter(clusterStat("upstream_cx_total"), Ge(2));
  const uint64_t started_one_host = counterValue("upstream_cx_preconnect_started");

  // Add a second host. It receives no demand traffic, so its floor is filled entirely by the
  // floor-fill path.
  setEdsHosts(makeHostIndices(0, 2));
  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_started"),
                               Ge(started_one_host + 2));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// A host removed via EDS has its connections drained.
TEST_P(EagerPreconnectFloorEdsIntegrationTest, DrainsHostRemovedViaEds) {
  concurrency_ = 1;
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* per_host = cluster->mutable_circuit_breakers()->add_per_host_thresholds();
    per_host->set_priority(envoy::config::core::v3::DEFAULT);
    per_host->mutable_max_connections()->set_value(2);
  });
  initializeEds(/*min_connections=*/2, /*initial_host_count=*/2);

  // Serving a request lets both hosts reach their floor.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(4));
  // The host that received no demand has its floor filled entirely by the eager floor-fill path.
  EXPECT_GE(counterValue("upstream_cx_preconnect_started"), 2);
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  // Removing one host drains its connections.
  setEdsHosts(makeHostIndices(0, 1));
  test_server_->waitForGauge(clusterStat("membership_total"), Eq(1));
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(2));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Budget & limits: floor maintenance respects circuit breakers, load shedding, and
// gives up on unreachable hosts.
// ---------------------------------------------------------------------------

// The circuit breaker caps floor maintenance to the available budget, holding the total below the floor.
TEST_P(EagerPreconnectFloorIntegrationTest, CircuitBreakerCapsFloorFill) {
  concurrency_ = 1;
  setMinConnections(3);
  setMaxConnections(2);
  initialize();

  serveOneRequest();

  // The breaker caps total connections at max_connections, below the floor of 3; the attempt to
  // exceed it trips overflow.
  test_server_->waitForCounter(clusterStat("upstream_cx_overflow"), Ge(1));
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(2));
  // The demand path fills the budget (2), then the pool's floor refill attempts the third
  // connection and is blocked by the breaker, so nothing is started eagerly.
  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_blocked"), Ge(1));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));

  cleanupUpstreamAndDownstream();
}

// Load-shed fixture: installs a fake resource monitor and a load-shed point so new upstream connections
// can be shed on demand.
class EagerPreconnectFloorLoadShedIntegrationTest : public BaseOverloadIntegrationTest,
                                                    public EagerPreconnectFloorIntegrationTest {
protected:
  void initializeWithLoadShedPoint(uint32_t min_connections) {
    setMinConnections(min_connections);
    setupOverloadManagerConfig(TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(
        R"EOF(
      name: "envoy.load_shed_points.connection_pool_new_connection"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
    updateResource(0);
  }
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorLoadShedIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// A load-shed point that blocks new upstream connections makes a floor maintenance intent count as blocked.
TEST_P(EagerPreconnectFloorLoadShedIntegrationTest, LoadShedCountsFloorFillAsBlocked) {
  concurrency_ = 1;
  initializeWithLoadShedPoint(/*min_connections=*/2);

  // Activate the load-shed point before any traffic. Every new upstream connection is now shed.
  updateResource(0.95);
  test_server_->waitForGauge(
      "overload.envoy.load_shed_points.connection_pool_new_connection.scale_percent", Eq(100));

  // Serve a request. The shed point blocks every new connection, so the floor maintenance intent
  // is counted as blocked.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  test_server_->waitForCounter(clusterStat("upstream_cx_preconnect_blocked"), Ge(1));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, gaugeValue("upstream_cx_active"));

  // Clear the overload so no floor maintenance spins during teardown.
  updateResource(0);
  cleanupUpstreamAndDownstream();
}

// Unreachable-host fixture. A TCP connect to a dead port is refused immediately (RST).
class EagerPreconnectFloorUnreachableHostIntegrationTest
    : public EagerPreconnectFloorIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorUnreachableHostIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// A host whose connections never establish and has no active HC does not reconnect forever.
TEST_P(EagerPreconnectFloorUnreachableHostIntegrationTest, StopsFillingUnreachableHost) {
  concurrency_ = 1;
  setMinConnections(2);
  // Point the single host at a dead port so every connect is refused before the handshake.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* lb_endpoint = bootstrap.mutable_static_resources()
                            ->mutable_clusters(0)
                            ->mutable_load_assignment()
                            ->mutable_endpoints(0)
                            ->mutable_lb_endpoints(0);
    lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(1);
  });
  config_helper_.skipPortUsageValidation();
  initialize();

  // Serving a request fails (the host is unreachable) but consumes the cluster as
  // HTTP, which triggers floor maintenance to the dead host.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  // A handful of connects are attempted and fail, latching the host and pausing floor maintenance.
  test_server_->waitForCounter(clusterStat("upstream_cx_connect_fail"), Ge(1));
  // ...so the pool drains to idle and stays there.
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(0));

  // Floor maintenance and connect attempts stop at the initial handful.
  EXPECT_LT(counterValue("upstream_cx_preconnect_started"), 10);
  EXPECT_LT(counterValue("upstream_cx_connect_fail"), 10);
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  cleanupUpstreamAndDownstream();
}

// ---------------------------------------------------------------------------
// Teardown: floor maintenance must not re-fill while shutting down or removing a cluster.
// ---------------------------------------------------------------------------

// Tearing the server down does not re-fill connections during destructAllConnections().
TEST_P(EagerPreconnectFloorIntegrationTest, NoRefillOnServerShutdown) {
  concurrency_ = 1;
  setMinConnections(3);
  initialize();

  serveOneRequest();
  test_server_->waitForGauge(clusterStat("upstream_cx_active"), Eq(3));
  // The served host's floor is filled by the demand path.
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));

  // Destroy the server with filled connections open; passing (no SIGABRT) is the assertion.
  cleanupUpstreamAndDownstream();
  test_server_.reset();
}

// CDS fixture: delivers cluster_0 via a filesystem CDS subscription so the whole cluster can be removed at runtime.
class EagerPreconnectFloorCdsIntegrationTest : public EagerPreconnectFloorEdsIntegrationTest {
protected:
  // Sources cluster_0 from CDS with the given floor, warms it empty, and starts the server.
  void initializeCds(uint32_t min_connections) {
    setUpstreamCount(1);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cds_config = bootstrap.mutable_dynamic_resources()->mutable_cds_config();
      cds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      cds_config->mutable_path_config_source()->set_path(cds_helper_.cdsPath());
      bootstrap.mutable_static_resources()->clear_clusters();
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    cluster_.set_name("cluster_0");
    cluster_.mutable_connect_timeout()->set_seconds(5);
    cluster_.set_type(envoy::config::cluster::v3::Cluster::EDS);
    auto* eds = cluster_.mutable_eds_cluster_config();
    eds->mutable_eds_config()->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    eds->mutable_eds_config()->mutable_path_config_source()->set_path(eds_helper_.edsPath());
    cluster_.mutable_preconnect_policy()->mutable_eager_preconnect_floor()->set_value(
        min_connections);
    envoy::config::endpoint::v3::ClusterLoadAssignment empty;
    empty.set_cluster_name("cluster_0");
    eds_helper_.setEds({empty});
    cds_helper_.setCds({cluster_});
    initialize();
    test_server_->waitForGauge("cluster_manager.warming_clusters", Eq(0));
  }

  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

// A CDS-delivered cluster does not carry the QUIC upstream transport socket.
INSTANTIATE_TEST_SUITE_P(
    Protocols, EagerPreconnectFloorCdsIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Removing a cluster via CDS drains its pools cleanly.
TEST_P(EagerPreconnectFloorCdsIntegrationTest, NoRefillDuringClusterRemoval) {
  concurrency_ = 1;
  initializeCds(/*min_connections=*/2);

  setEdsHosts(makeHostIndices(0, 1));
  // Serving a request fills the host's floor.
  serveOneRequest();
  test_server_->waitForCounter(clusterStat("upstream_cx_total"), Ge(2));
  // The served host's floor is filled by the demand path.
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_started"));
  EXPECT_EQ(0, counterValue("upstream_cx_preconnect_blocked"));
  cleanupUpstreamAndDownstream();

  // Reaching a completed removal proves the drain is clean.
  cds_helper_.setCds({});
  test_server_->waitForCounter("cluster_manager.cluster_removed", Ge(1));
}

} // namespace
} // namespace Envoy
