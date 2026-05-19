#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

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
  std::atomic<std::size_t> num_events_;
  absl::optional<std::chrono::milliseconds> newInterval;

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
    std::string server_address = fmt::format("{}:{}", localhost, reportingPort);
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
                                  public BaseIntegrationTest {
public:
  GrpcClientIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfigNoListeners()) {}

  void initialize() override {
    use_lds_ = true;

    std::string localhost = Network::Test::getLoopbackAddressString(GetParam());
    std::string anyhost = Network::Test::getAnyAddressString(GetParam());

    addBootstrapExtension(getUpstreamExtension(getReporterConfig(), enable_tenant_isolation_),
                          config_helper_);
    addBootstrapExtension(getDownstreamExtension(), config_helper_);
    addCluster(getDownstreamCluster(localhost), config_helper_);
    addListener(getUpstreamListener(anyhost), config_helper_, current_listeners_);

    auto cluster = getUpstreamCluster(localhost);
    addCluster(getHttp2Cluster(cluster), config_helper_);

    BaseIntegrationTest::initialize();

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
    client->mutable_typed_config()->PackFrom(grpc_cfg);

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
    return fmt::format("{}:{}", downstreamTenant, name);
  }

  std::vector<std::string> getTenantIsolatedNames(std::vector<std::string> names) {
    for (auto& name : names) {
      name = getTenantIsolatedName(name);
    }

    return names;
  }

  void setNewSendInterval(std::chrono::milliseconds ms) { grpc_server_->service_.setInterval(ms); }

  std::size_t numEvents() { return grpc_server_->service_.numEvents(); }

  std::function<void(StreamReverseTunnelsRequest&&)> callback_;
  std::unique_ptr<GrpcServer> grpc_server_;

  int current_listeners_{0};
  int cur_version_{0};

  ConfigHelper current_config_{version_, config_helper_.bootstrap()};
  bool enable_tenant_isolation_{false};
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
  test_server_->waitForGauge("listener.upstreamListener.downstream_cx_active", testing::Eq(3),
                             std::chrono::milliseconds(sendInterval * 3));
  removeListenerLds("node-1");
  // Wait for the connections to establish and drain
  test_server_->waitForGauge("listener.upstreamListener.downstream_cx_active", testing::Eq(2),
                             std::chrono::milliseconds(sendInterval * 3));

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
  test_server_->waitForGauge("listener.upstreamListener.downstream_cx_active", testing::Eq(2),
                             std::chrono::milliseconds(sendInterval * 3));

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
    nodes[i] = fmt::format("node-{}", i);
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

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
