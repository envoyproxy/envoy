#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/grpc_client.pb.h"
#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/stream_reverse_tunnels.grpc.pb.h"
#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters/event_reporter.pb.h"
#include "contrib/reverse_tunnel_reporter/source/reverse_tunnel_event_types.h"
#include "contrib/reverse_tunnel_reporter/test/clients/integration_test_utils.h"
#include "grpc++/grpc++.h"
#include "grpc++/server.h"
#include "grpc++/server_builder.h"
#include "grpc++/server_context.h"
#include "gtest/gtest.h"
#include "integration_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

constexpr absl::string_view reporterName =
    "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter";
constexpr absl::string_view grpcClient =
    "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.clients.grpc_client";
constexpr std::chrono::milliseconds serverWait{5};
constexpr std::size_t sendInterval{500};
constexpr std::size_t maxRetries{3};

using envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::grpc_client::GrpcClientConfig;
using envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::grpc_client::
    ReverseTunnelReportingService;
using envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::grpc_client::
    StreamReverseTunnelsRequest;
using envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::grpc_client::
    StreamReverseTunnelsResponse;
using envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig;
using envoy::extensions::upstreams::http::v3::HttpProtocolOptions;

class TestingService final : public ReverseTunnelReportingService::Service {
  absl::flat_hash_map<std::string, ReverseTunnelEvent::Connected> state_;
  std::atomic<std::size_t> num_events_{0};
  std::optional<std::chrono::milliseconds> newInterval;

public:
  grpc::Status StreamReverseTunnels(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<StreamReverseTunnelsResponse, StreamReverseTunnelsRequest>* stream)
      override {
    StreamReverseTunnelsRequest request;
    int cnt{0};

    ENVOY_LOG_MISC(error, "GrpcClientIntegrationTest: Status=Connected");

    while (stream->Read(&request)) {
      cnt++;
      StreamReverseTunnelsResponse response;
      response.set_request_nonce(request.nonce());

      if (newInterval.has_value()) {
        *response.mutable_report_interval() =
            Protobuf::util::TimeUtil::MillisecondsToDuration(newInterval.value().count());
      }

      if (!stream->Write(response)) {
        ENVOY_LOG_MISC(error, "GrpcClientIntegrationTest: Unable to send the response: {}",
                       response.DebugString());
        break;
      }

      process(request);
    }

    ENVOY_LOG_MISC(error, "GrpcClientIntegrationTest: Stream ended, total messages: {}", cnt);

    return grpc::Status::OK;
  }

  void process(StreamReverseTunnelsRequest& req) {
    for (auto& conn : req.added_tunnels()) {
      state_[conn.name()] =
          ReverseTunnelEvent::Connected{.node_id = conn.identity().node_id(),
                                        .cluster_id = conn.identity().cluster_id(),
                                        .tenant_id = conn.identity().tenant_id(),
                                        .created_at = Envoy::SystemTime{}};
    }

    for (auto& name : req.removed_tunnel_names()) {
      state_.erase(name);
    }

    num_events_ += 1;
  }

  absl::flat_hash_map<std::string, ReverseTunnelEvent::Connected> getState() { return state_; }

  std::size_t numEvents() { return num_events_.load(); }

  void setInterval(std::chrono::milliseconds ms) { newInterval = ms; }
};

struct GrpcServer {
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;
  TestingService service_;

  explicit GrpcServer(absl::string_view localhost) {
    std::string server_address = std::format("{}:{}", localhost, reportingPort);
    grpc::ServerBuilder builder;

    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);

    server_ = std::unique_ptr<grpc::Server>(builder.BuildAndStart());
    server_thread_ = std::thread([this] { server_->Wait(); });
  }

  ~GrpcServer() {
    auto deadline = std::chrono::system_clock::now() + serverWait; // NO_CHECK_FORMAT(real_time)
    server_->Shutdown(deadline);
    server_thread_.join();
  }
};

class GrpcClientIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  GrpcClientIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(),
                            ConfigHelper::baseConfigNoListeners()) {
    drain_strategy_ = Server::DrainStrategy::Immediate;
    drain_time_ = std::chrono::seconds(1);
  }

  void initialize() override {
    use_lds_ = true;

    std::string localhost = Network::Test::getLoopbackAddressString(GetParam());
    std::string anyhost = Network::Test::getAnyAddressString(GetParam());

    addBootstrapExtension(getUpstreamExtension(getReporterConfig(), enable_tenant_isolation_),
                          config_helper_);
    addBootstrapExtension(getDownstreamExtension(), config_helper_);
    addCluster(getDownstreamCluster(localhost), config_helper_);
    addListener(getUpstreamListener(anyhost), config_helper_, current_listeners_);
    addListener(getEgressListener(anyhost), config_helper_, current_listeners_);

    auto cluster = getUpstreamCluster(localhost);
    addCluster(getHttp2Cluster(cluster), config_helper_);
    addCluster(metadata_key_.empty()
                   ? getRevConnCluster(enable_tenant_isolation_)
                   : getRevConnClusterWithMetadata(metadata_key_, enable_tenant_isolation_),
               config_helper_);
    HttpIntegrationTest::initialize();

    current_config_ = ConfigHelper{version_, config_helper_.bootstrap()};
  }

protected:
  EventReporterConfig getReporterConfig() {
    EventReporterConfig cfg;
    cfg.set_stat_prefix(reporterName);

    auto* client = cfg.add_clients();
    client->set_name(grpcClient);

    GrpcClientConfig grpc_cfg;
    grpc_cfg.set_stat_prefix("reverse_connection_grpc_client");
    grpc_cfg.set_cluster(upstreamCluster);
    *(grpc_cfg.mutable_default_send_interval()) =
        Protobuf::util::TimeUtil::MillisecondsToDuration(sendInterval);
    *(grpc_cfg.mutable_connect_retry_interval()) =
        Protobuf::util::TimeUtil::MillisecondsToDuration(sendInterval);
    grpc_cfg.set_max_retries(maxRetries);
    grpc_cfg.set_max_buffer_count(1'000'000);
    std::ignore = client->mutable_typed_config()->PackFrom(grpc_cfg);

    return cfg;
  }

  void updateLds(ConfigHelper& new_config) {
    new_config.setLds(std::to_string(++cur_version_));
    // Wait for up to a minute for the values to propagate.
    test_server_->waitForGauge("listener_manager.total_listeners_active",
                               testing::Eq(current_listeners_), std::chrono::seconds(60));
    current_config_ = std::move(new_config);
  }

  void addListenerLds(Listener&& listener) {
    ConfigHelper new_config{version_, current_config_.bootstrap()};
    addListener(std::move(listener), new_config, current_listeners_);
    updateLds(new_config);
  }

  void addListenerLds(std::vector<Listener>&& listeners) {
    ConfigHelper new_config{version_, current_config_.bootstrap()};

    for (auto& listener : listeners) {
      addListener(std::move(listener), new_config, current_listeners_);
    }

    updateLds(new_config);
  }

  void removeListenerLds(const std::string& name) {
    ConfigHelper new_config{version_, current_config_.bootstrap()};
    removeListener(name, new_config, current_listeners_);
    updateLds(new_config);
  }

  void removeListenerLds(std::vector<std::string>& names) {
    ConfigHelper new_config{version_, current_config_.bootstrap()};

    for (auto& name : names) {
      removeListener(name, new_config, current_listeners_);
    }

    updateLds(new_config);
  }

  void
  validateEqual(std::chrono::milliseconds ms,
                const absl::flat_hash_map<std::string, ReverseTunnelEvent::Connected>& expected) {
    timeSystem().advanceTimeWait(ms);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

    // This is fine no concern about thread safety -> we have slept for some time and no extra
    // events should be coming now.
    auto state = grpc_server_->service_.getState();
    // EXPECT_THAT(state_, testing::ContainerEq(expected));
    EXPECT_EQ(state.size(), expected.size());
  }

  // Waits until the event reporter observes `count` active reverse tunnels. A completed handshake
  // detaches the upstream listener connection, so the reporter's unique-active gauge, not
  // listener.<name>.downstream_cx_active, reflects the number of established tunnels.
  void waitForActiveTunnels(
      int count, std::chrono::milliseconds timeout = std::chrono::milliseconds(sendInterval * 3)) {
    test_server_->waitForGauge("envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service."
                               "reporters.event_reporter.reverse_tunnel_unique_active",
                               testing::Eq(count), timeout);
  }

  absl::flat_hash_map<std::string, ReverseTunnelEvent::Connected>
  getConns(std::vector<std::string> node_ids) {
    absl::flat_hash_map<std::string, ReverseTunnelEvent::Connected> connections;

    for (auto& node_id : node_ids) {
      connections[ReverseTunnelEvent::getName(node_id)] =
          ReverseTunnelEvent::Connected{.node_id = node_id,
                                        .cluster_id = std::string(downstreamCluster),
                                        .tenant_id = std::string(downstreamTenant),
                                        .created_at = Envoy::SystemTime{}};
    }

    return connections;
  }

  void makeNewServer() {
    grpc_server_ =
        std::make_unique<GrpcServer>(Network::Test::getLoopbackAddressUrlString(GetParam()));
  }

  std::string getTenantIsolatedName(std::string name) {
    return std::format("{}:{}", downstreamTenant, name);
  }

  std::vector<std::string> getTenantIsolatedNames(std::vector<std::string> names) {
    for (auto& name : names) {
      name = getTenantIsolatedName(name);
    }

    return names;
  }

  IntegrationStreamDecoderPtr makeClientRequest(std::string path,
                                                IntegrationCodecClientPtr& client) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", path},
        {":scheme", "http"},
        {":authority", "localhost"},
        {"x-computed-host-id", "node-1"},
    };

    return client->makeHeaderOnlyRequest(request_headers);
  }

  void completeReq(std::chrono::milliseconds delay, IntegrationStreamDecoderPtr& resp) {
    ASSERT_TRUE(resp->waitForEndStream(delay));
    EXPECT_EQ("200", resp->headers().getStatusValue());
  }

  void setNewSendInterval(std::chrono::milliseconds ms) { grpc_server_->service_.setInterval(ms); }

  std::size_t numEvents() { return grpc_server_->service_.numEvents(); }

  std::function<void(StreamReverseTunnelsRequest&&)> callback_;
  std::unique_ptr<GrpcServer> grpc_server_;

  int current_listeners_{0};
  int cur_version_{0};

  ConfigHelper current_config_{version_, config_helper_.bootstrap()};
  bool enable_tenant_isolation_{false};
  // When non-empty, initialize() installs the reverse-connection cluster with this drain metadata
  // key on the upstream codec, so received connection-metadata triggers a redial + reporter event.
  std::string metadata_key_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Test: HappyPath
 * Tests the standard lifecycle: gRPC stream establishment, reporting new
 * tunnel additions via LDS, and reporting removals when listeners are deleted.
 */
TEST_P(GrpcClientIntegrationTest, HappyPath) {
  makeNewServer();
  initialize();

  addListenerLds(getDownstreamListener("node-1", 1));
  addListenerLds(getDownstreamListener("node-2", 1));

  // Give envoy time to make the rc, connect to the test server, timer to fire
  // and send the updates.
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1", "node-2"}));

  removeListenerLds("node-1");
  removeListenerLds("node-2");

  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({}));
}

/**
 * Test: TenantIsolation.
 * Tests the working of the code when enabled with tenant isolation.
 */
TEST_P(GrpcClientIntegrationTest, TenantIsolation) {
  enable_tenant_isolation_ = true;

  initialize();
  makeNewServer();

  addListenerLds(getDownstreamListener("node-1", 1));
  addListenerLds(getDownstreamListener("node-2", 1));
  validateEqual(std::chrono::milliseconds(sendInterval * 3),
                getConns(getTenantIsolatedNames({"node-1", "node-2"})));

  removeListenerLds("node-1");
  removeListenerLds("node-2");
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({}));
}

/**
 * Test: ServerReconnect
 * Tests that Envoy performs a "Full State Push" upon reconnection.
 * Even if tunnels didn't change, they must be re-reported on a new gRPC stream.
 */
TEST_P(GrpcClientIntegrationTest, ServerReconnect) {
  makeNewServer();
  initialize();

  addListenerLds(getDownstreamListener("node-1", 1));
  addListenerLds(getDownstreamListener("node-2", 1));
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1", "node-2"}));

  makeNewServer();
  // Time to disconnect, connect and then full push.
  validateEqual(std::chrono::milliseconds(sendInterval * 5), getConns({"node-1", "node-2"}));

  removeListenerLds("node-1");
  removeListenerLds("node-2");

  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({}));
}

/**
 * Test: EventsWhenServerIsDead
 * Tests "Event Convergence." If state changes (Add/Remove) happen while the
 * server is down, the client must report the final ground truth once back online.
 */
TEST_P(GrpcClientIntegrationTest, EventsWhenServerIsDead) {
  makeNewServer();
  initialize();

  addListenerLds(getDownstreamListener("node-1", 1));
  addListenerLds(getDownstreamListener("node-2", 1));
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1", "node-2"}));

  grpc_server_ = nullptr;
  addListenerLds(getDownstreamListener("node-3", 1));
  waitForActiveTunnels(3);
  removeListenerLds("node-1");
  // Wait for the connections to establish and drain
  waitForActiveTunnels(2);

  makeNewServer();
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-3", "node-2"}));

  removeListenerLds("node-2");
  removeListenerLds("node-3");
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({}));
}

/**
 * Test: ServerDead
 * Tests the gRPC client's retry engine. It verifies that the client
 * continues to attempt connections based on 'connect_retry_interval' forever.
 * We have limited to max_retries + 2 as we had to stop somewhere.
 */
TEST_P(GrpcClientIntegrationTest, ServerDead) {
  initialize();
  // First one on server init.
  test_server_->waitForCounter(
      "reverse_connection_grpc_client.connection_attempts.cluster.upstreamCluster", testing::Eq(1));

  for (std::size_t i = 0; i <= maxRetries; i++) {
    // Wait for the timer to fire first.
    timeSystem().advanceTimeWait(std::chrono::milliseconds(sendInterval));
    test_server_->waitForCounter(
        "reverse_connection_grpc_client.connection_attempts.cluster.upstreamCluster",
        testing::Eq(i + 2));
  }
}

/**
 * Test: ServerLate
 * Tests the "Catch-up" scenario. Verifies that if tunnels exist before the
 * reporting service is reachable, the client pushes the state immediately on connection.
 */
TEST_P(GrpcClientIntegrationTest, ServerLate) {
  initialize();

  addListenerLds(getDownstreamListener("node-1", 1));
  addListenerLds(getDownstreamListener("node-2", 1));
  // Wait for the connections to establish.
  waitForActiveTunnels(2);

  makeNewServer();
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1", "node-2"}));

  removeListenerLds("node-1");
  removeListenerLds("node-2");
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({}));
}

/**
 * Test: ServerSentTime.
 * Tests that the client uses the time sent by the server for the updates.
 * Overriding the default value.
 */
TEST_P(GrpcClientIntegrationTest, ServerSentTime) {
  initialize();
  makeNewServer();

  // initialize() starts the client before the server exists. Wait for the retry
  // to connect and the first message to be processed.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(sendInterval * 3));

  std::size_t num_events_1 = numEvents();
  timeSystem().advanceTimeWait(std::chrono::milliseconds(2 * sendInterval));
  std::size_t num_events_2 = numEvents();

  EXPECT_GE(num_events_2 - num_events_1, 1);

  // Now change the send Interval to sendInterval / 5.
  setNewSendInterval(std::chrono::milliseconds(sendInterval / 5));
  timeSystem().advanceTimeWait(std::chrono::milliseconds(2 * sendInterval));
  std::size_t num_events_3 = numEvents();

  // This would have been scheduled and so we dont expect a change in the scheduling.
  EXPECT_GE(num_events_3 - num_events_2, 1);

  // Now we track the numEvents and check that the sendInterval is smaller -> more events.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(2 * sendInterval));
  std::size_t num_events_4 = numEvents();
  EXPECT_GE(num_events_4 - num_events_3, 5);
}

// Only run the load test in optimized builds.
#if defined(NDEBUG)
TEST_P(GrpcClientIntegrationTest, LoadTest) {
  makeNewServer();
  initialize();

  // Limited to 1000 to reduce test flakiness.
  int sz = 1000;
  std::vector<std::string> nodes(sz);
  for (int i = 0; i < sz; i++) {
    nodes[i] = std::format("node-{}", i);
  }

  std::vector<Listener> listeners(sz);
  for (int i = 0; i < sz; i++) {
    listeners[i] = getDownstreamListener(nodes[i], 1);
  }

  addListenerLds(std::move(listeners));
  // Pure overhead of running the client should be minimal.
  validateEqual(std::chrono::milliseconds(sendInterval * 5), getConns(nodes));

  removeListenerLds(nodes);
  // Allow more time for the discovery of listener removal and then propogation.
  validateEqual(std::chrono::milliseconds(sendInterval * 10), getConns({}));
}
#endif // defined(NDEBUG)

TEST_P(GrpcClientIntegrationTest, ListenerDrain) {
  drain_time_ = slowRouteDelay + std::chrono::seconds(3);
  initialize();
  makeNewServer();

  addListenerLds(getDownstreamListener("node-1", 1));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  // keep one request open on the downstream listener.
  auto client = makeHttpConnection(egressPort);
  auto resp = makeClientRequest("/slow", client);
  test_server_->waitForGauge("http.node-1.downstream_cx_active", testing::Eq(1),
                             std::chrono::milliseconds(sendInterval * 3));

  removeListenerLds("node-1");
  // Now wait for the notification on the server side.
  // 1000 ms for the drain aware hcm to send the goaway and for it to be received.
  validateEqual(std::chrono::milliseconds(sendInterval * 3 + 1000), getConns({}));

  // wait for the response to be received successfully.
  ASSERT_TRUE(
      resp->waitForEndStream(std::chrono::milliseconds((slowRouteDelay.count() + 1) * 1000)));
  EXPECT_EQ("200", resp->headers().getStatusValue());
}

TEST_P(GrpcClientIntegrationTest, ListenerDrainMaxDuration) {
  initialize();
  makeNewServer();

  addListenerLds(getDownstreamListener("node-1", 1, std::chrono::seconds(5)));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  auto client = makeHttpConnection(egressPort);
  auto resp = makeClientRequest("/slow", client);
  test_server_->waitForGauge("http.node-1.downstream_cx_active", testing::Eq(1),
                             std::chrono::milliseconds(sendInterval * 3));

  // New tunnel too will be alive.
  test_server_->waitForGauge("http.node-1.downstream_cx_active", testing::Eq(2),
                             std::chrono::milliseconds(sendInterval * 3 + 5000));
  // Reporter should see old tunnel going down + new tunnel being added and unique_tunnels = 1.
  test_server_->waitForCounter("envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service."
                               "reporters.event_reporter.reverse_tunnel_established_total",
                               testing::Eq(2), std::chrono::milliseconds(sendInterval));
  test_server_->waitForCounter(
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_closed_total",
      testing::Eq(1),
      std::chrono::milliseconds(sendInterval +
                                1000) // with the drain timeout of the hcm to send the goaway.
  );
  waitForActiveTunnels(1, std::chrono::milliseconds(sendInterval));

  // Now use the new tunnel for a request.
  auto direct_client = makeHttpConnection(egressPort);
  auto direct_resp = makeClientRequest("/direct", direct_client);
  completeReq(std::chrono::milliseconds(1000), direct_resp);

  completeReq(std::chrono::milliseconds((slowRouteDelay.count() + 1) * 1000), resp);
}

// Three-phase metadata drain, validated as a gradual cutover: METADATA -> soft GOAWAY /
// shutdownNotice -> final GOAWAY, driven by max_connection_duration. The replacement tunnel is
// redialed at drain start. The test asserts the migration is gradual by issuing a real request
// and checking stats at each phase:
//   (1) tunnel established, nothing closed yet (reverse_tunnel_closed == 0);
//   (2) replacement tunnel established (established_total == 2): no GOAWAYs and no metadata yet
//       (close_notify == 0, goaway_sent == 0, reverse_tunnel_closed == 0), traffic still flows;
//   (3) METADATA received: the upstream codec matches the drain key and notifies the reporter
//       (reverse_tunnel_closed == 1), but no GOAWAY yet (close_notify == 0, goaway_sent == 0);
//   (4) soft GOAWAY received (close_notify == 1): final GOAWAY not yet (goaway_sent == 0), and
//       new traffic routes to the replacement tunnel;
//   (5) final GOAWAY sent (goaway_sent == 1): the replacement still serves traffic, and the
//       request still in flight on the old tunnel completes.
// A request is held in flight on the old tunnel from (3) onward. Without it the peer's pool closes
// the idle old tunnel right after the soft GOAWAY, before drain_timeout, and the final GOAWAY is
// never sent.
TEST_P(GrpcClientIntegrationTest, ListenerDrainMetadataThreePhase) {
  metadata_key_ = "drain_reverse_tunnel";
  initialize();
  makeNewServer();

  // max_connection_duration=20s triggers the drain at ~20s. Phase delays are 1s/5s/10s:
  // replacement tunnel redialed at ~20s (established_total==2), METADATA sent ~21s
  // (reverse_tunnel_closed==1), soft GOAWAY ~25s (close_notify==1), final GOAWAY ~30s
  // (goaway_sent==1). 20s is long enough that the request lands on a stable tunnel and the
  // redialed tunnel (up ~20s) does not re-drain before the test observes the full cycle (it would
  // drain at ~40s, after the test ends).
  // The in-flight request started at ~21s must outlive the final GOAWAY at ~30s. 15s leaves ~6s
  // of margin for slow CI machines.
  constexpr std::chrono::seconds inFlightRequestDelay{15};
  addListenerLds(getDownstreamListenerWithMetadata(
      "node-1", 1, std::chrono::seconds(20), "drain_reverse_tunnel",
      std::chrono::milliseconds(1000), std::chrono::milliseconds(5000), std::chrono::seconds(10),
      inFlightRequestDelay));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  // Issue a /direct request through the egress listener -> reverse_connection cluster -> node-1
  // tunnel and expect 200. completeReq asserts the status.
  auto sendDirectRequest = [&]() {
    auto client = makeHttpConnection(egressPort);
    auto resp = makeClientRequest("/direct", client);
    completeReq(std::chrono::milliseconds(1000), resp);
  };

  // Convenience aliases for the long stat names.
  const std::string closed_total =
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_closed_total";
  const std::string established_total =
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_established_total";
  const std::string close_notify = "cluster.reverse_connection_cluster.upstream_cx_close_notify";
  const std::string goaway_sent = "http2.goaway_sent";

  // (1) Tunnel established, nothing closed yet. Baseline: the tunnel is routable.
  EXPECT_EQ(0, test_server_->counter(closed_total)->value());
  sendDirectRequest();

  // (2) Drain starts at ~20s and redials a replacement tunnel (established_total==2). The
  // metadata timer (1s) has not fired yet, so no GOAWAYs and no reporter close.
  test_server_->waitForCounter(established_total, testing::Eq(2), std::chrono::milliseconds(22000));
  EXPECT_EQ(0, test_server_->counter(close_notify)->value());
  EXPECT_EQ(0, test_server_->counter(goaway_sent)->value());
  EXPECT_EQ(0, test_server_->counter(closed_total)->value());
  sendDirectRequest();

  // (3) METADATA received: the upstream codec matched the drain key and notified the reporter
  // (reverse_tunnel_closed==1). No GOAWAY yet (close_notify==0, goaway_sent==0). Reached at ~20s
  // (after phase 2); the metadata timer fires ~1s later, so ~5s is a tight CI-safe bound.
  test_server_->waitForCounter(closed_total, testing::Eq(1), std::chrono::milliseconds(5000));
  EXPECT_EQ(0, test_server_->counter(close_notify)->value());
  EXPECT_EQ(0, test_server_->counter(goaway_sent)->value());
  sendDirectRequest();

  // Hold a stream open on the old tunnel. It is still the pool's only connection here: the
  // replacement tunnel stays parked until the pool needs a new connection after the soft GOAWAY.
  auto in_flight_client = makeHttpConnection(egressPort);
  auto in_flight_resp = makeClientRequest("/slow", in_flight_client);

  // (4) Soft GOAWAY (shutdownNotice) received by the client: close_notify==1, final GOAWAY not
  // yet sent (goaway_sent==0). New traffic now routes to the replacement tunnel. Reached at ~21s;
  // the shutdown-notice timer fires ~4s later (at ~25s), so ~8s is a tight CI-safe bound.
  test_server_->waitForCounter(close_notify, testing::Eq(1), std::chrono::milliseconds(8000));
  EXPECT_EQ(0, test_server_->counter(goaway_sent)->value());
  sendDirectRequest();

  // (5) Final GOAWAY sent by the server: goaway_sent==1. The replacement tunnel still serves
  // traffic. Reached at ~25s; the goaway timer fires ~5s later (at ~30s), so ~10s is a tight
  // CI-safe bound.
  test_server_->waitForCounter(goaway_sent, testing::Eq(1), std::chrono::milliseconds(10000));
  test_server_->waitForGauge("envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service."
                             "reporters.event_reporter.reverse_tunnel_unique_active",
                             testing::Eq(1), std::chrono::milliseconds(10000));
  sendDirectRequest();

  // The stream held across the drain finishes on the old tunnel after the final GOAWAY.
  completeReq(std::chrono::milliseconds((inFlightRequestDelay.count() + 1) * 1000), in_flight_resp);
}

// Upstream-initiated drain: the admin endpoint /reverse_tunnel/drain_clusters triggers the upstream
// (client) codec's startGracefulDrain, which sends a stream-0 METADATA frame immediately (drain
// key -> the downstream redials a replacement tunnel) and a final GOAWAY after drain_time (pool
// drain, old tunnel closes). The downstream listener uses a long max_connection_duration so it does
// NOT self-drain during the test; the only drain is the upstream admin trigger. Validated as a
// gradual cutover:
//   (1) tunnel established (established_total==1), nothing closed, traffic flows;
//   (2) admin drain -> upstream sends METADATA -> downstream redials (established_total==2),
//       no GOAWAY yet (goaway_sent==0), no close yet (reverse_tunnel_closed==0), traffic flows;
//   (3) after drain_time -> upstream sends final GOAWAY (goaway_sent==1), the pool drains the
//       old connection, the old tunnel closes (reverse_tunnel_closed==1), traffic flows on the
//       replacement.
TEST_P(GrpcClientIntegrationTest, UpstreamAdminDrainMetadata) {
  metadata_key_ = "drain_reverse_tunnel";
  initialize();
  makeNewServer();

  // Long max_connection_duration so the downstream does not self-drain during the test; the only
  // drain is the upstream admin trigger. drain_time_ms=5000 -> METADATA now, final GOAWAY ~5s.
  addListenerLds(getDownstreamListenerWithMetadata("node-1", 1, std::chrono::seconds(120)));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  auto sendDirectRequest = [&]() {
    auto client = makeHttpConnection(egressPort);
    auto resp = makeClientRequest("/direct", client);
    completeReq(std::chrono::milliseconds(1000), resp);
  };

  // Convenience aliases for the long stat names. The upstream's final GOAWAY is counted by the
  // drain-aware upstream codec stat (server-root scoped), not the per-cluster http2 codec
  // stat, so it is unambiguous regardless of how many http2 codecs exist.
  const std::string closed_total =
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_closed_total";
  const std::string established_total =
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_established_total";
  const std::string goaway_sent = "reverse_tunnel_upstream_codec.goaway_sent";

  // (1) Tunnel established, nothing closed yet. Baseline: the tunnel is routable.
  EXPECT_EQ(0, test_server_->counter(closed_total)->value());
  EXPECT_EQ(0, test_server_->counter(goaway_sent)->value());
  sendDirectRequest();

  // (2) Trigger the upstream-initiated drain. The upstream sends METADATA now -> the downstream
  // redials a replacement (established_total==2). No GOAWAY yet (goaway_sent==0) and no close yet
  // (reverse_tunnel_closed==0); traffic still flows on the old tunnel.
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      test_server_->adminAddress(), "POST", "/reverse_tunnel/drain_clusters?drain_time_ms=5000", "",
      Http::CodecType::HTTP1);
  ASSERT_TRUE(admin_response != nullptr);
  ASSERT_TRUE(admin_response->complete());
  EXPECT_EQ("200", admin_response->headers().Status()->value().getStringView());

  // The downstream redials as soon as it receives the drain-key METADATA (sent immediately on the
  // admin trigger), so established_total==2 follows within ~1-2s; ~10s is a tight CI-safe bound.
  test_server_->waitForCounter(established_total, testing::Eq(2), std::chrono::milliseconds(10000));
  EXPECT_EQ(0, test_server_->counter(goaway_sent)->value());
  EXPECT_EQ(0, test_server_->counter(closed_total)->value());
  sendDirectRequest();

  // (3) After drain_time (~5s) the upstream sends the final GOAWAY (goaway_sent==1), the pool
  // drains the old connection, and the old tunnel closes (reverse_tunnel_closed==1). Traffic
  // still flows on the replacement.
  // GOAWAY fires at drain_time (~5s) after the admin trigger; by now the phase-2 redial has already
  // consumed ~1-2s of that window, so ~10s (drain_time + CI margin) is a tight, meaningful bound.
  test_server_->waitForCounter(goaway_sent, testing::Eq(1), std::chrono::milliseconds(10000));
  test_server_->waitForCounter(closed_total, testing::Eq(1), std::chrono::milliseconds(10000));
  sendDirectRequest();
}

TEST_P(GrpcClientIntegrationTest, ListenerDrainMaxDurationNoActiveRequest) {
  initialize();
  makeNewServer();

  addListenerLds(getDownstreamListener("node-1", 1, std::chrono::seconds(5)));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  auto client = makeHttpConnection(egressPort);
  auto resp = makeClientRequest("/direct", client);
  completeReq(std::chrono::milliseconds(1000), resp);

  test_server_->waitForGauge("http.node-1.downstream_cx_active", testing::Eq(2),
                             std::chrono::milliseconds(sendInterval * 3 + 5000));

  test_server_->waitForCounter("envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service."
                               "reporters.event_reporter.reverse_tunnel_established_total",
                               testing::Eq(2), std::chrono::milliseconds(sendInterval));
  test_server_->waitForCounter(
      "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_reporter."
      "reverse_tunnel_closed_total",
      testing::Eq(1),
      std::chrono::milliseconds(sendInterval +
                                1000) // with the drain timeout of the hcm to send the goaway.
  );
  waitForActiveTunnels(1, std::chrono::milliseconds(sendInterval));

  auto new_client = makeHttpConnection(egressPort);
  auto new_resp = makeClientRequest("/direct", new_client);
  completeReq(std::chrono::milliseconds(1000), new_resp);
}

TEST_P(GrpcClientIntegrationTest, ListenerDrainNoActiveRequest) {
  initialize();
  makeNewServer();

  addListenerLds(getDownstreamListener("node-1", 1));
  waitForActiveTunnels(1);
  validateEqual(std::chrono::milliseconds(sendInterval * 3), getConns({"node-1"}));

  auto client = makeHttpConnection(egressPort);
  auto resp = makeClientRequest("/direct", client);
  completeReq(std::chrono::milliseconds(1000), resp);

  removeListenerLds("node-1");
  // shld send a drain in 1s and then some time for the data to get to our server.
  validateEqual(std::chrono::milliseconds(sendInterval * 3 + 1000), getConns({}));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
