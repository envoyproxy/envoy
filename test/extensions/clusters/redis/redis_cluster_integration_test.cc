#include <sstream>
#include <vector>

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/integration/integration.h"

using testing::Return;

namespace Envoy {
namespace {

// This is a basic redis_proxy configuration with a single host
// in the cluster. The load balancing policy must be set
// to random for proper test operation.

const std::string CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      lb_policy: RANDOM
      hosts:
      - socket_address:
          address: 127.0.0.1
          port_value: 0
      cluster_type:
        name: envoy.clusters.redis
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            cluster_refresh_rate: 1s
            cluster_refresh_timeout: 4s
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.redis_proxy
        config:
          stat_prefix: redis_stats
          cluster: cluster_0
          settings:
            op_timeout: 5s
)EOF";

// This function encodes commands as an array of bulkstrings as transmitted by Redis clients to
// Redis servers, according to the Redis protocol.
std::string makeBulkStringArray(std::vector<std::string>&& command_strings) {
  std::stringstream result;

  result << "*" << command_strings.size() << "\r\n";
  for (uint64_t i = 0; i < command_strings.size(); i++) {
    result << "$" << command_strings[i].size() << "\r\n";
    result << command_strings[i] << "\r\n";
  }

  return result.str();
}

class RedisClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {
public:
  RedisClusterIntegrationTest(const std::string& config = CONFIG, int num_upstreams = 2)
      : BaseIntegrationTest(GetParam(), config), num_upstreams_(num_upstreams),
        version_(GetParam()) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    setUpstreamCount(num_upstreams_);
    setDeterministic();
    config_helper_.renameListener("redis_proxy");

    // Change the port for each of the discovery host in cluster_0.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      uint32_t upstream_idx = 0;
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);

      for (int j = 0; j < cluster_0->hosts_size(); ++j) {
        if (cluster_0->mutable_hosts(j)->has_socket_address()) {
          auto* host_socket_addr = cluster_0->mutable_hosts(j)->mutable_socket_address();
          RELEASE_ASSERT(fake_upstreams_.size() > upstream_idx, "");
          host_socket_addr->set_address(
              fake_upstreams_[upstream_idx]->localAddress()->ip()->addressAsString());
          host_socket_addr->set_port_value(
              fake_upstreams_[upstream_idx++]->localAddress()->ip()->port());
        }
      }
    });

    BaseIntegrationTest::initialize();

    mock_rng_ = dynamic_cast<Runtime::MockRandomGenerator*>(&test_server_->server().random());
    // Abort now if we cannot downcast the server's random number generator pointer.
    ASSERT_TRUE(mock_rng_ != nullptr);
    // Ensure that fake_upstreams_[0] is the load balancer's host of choice by default.
    ON_CALL(*mock_rng_, random()).WillByDefault(Return(random_index_));
  }

protected:
  /**
   * Simple bi-directional test between a fake Redis client and Redis server.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   */
  void simpleRequestAndResponse(const int stream_index, const std::string& request,
                                const std::string& response) {
    std::string proxy_to_server;
    IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
    redis_client->write(request);

    FakeRawConnectionPtr fake_upstream_connection;
    EXPECT_TRUE(fake_upstreams_[stream_index]->waitForRawConnection(fake_upstream_connection));
    EXPECT_TRUE(fake_upstream_connection->waitForData(request.size(), &proxy_to_server));
    // The original request should be the same as the data received by the server.
    EXPECT_EQ(request, proxy_to_server);

    EXPECT_TRUE(fake_upstream_connection->write(response));
    redis_client->waitForData(response);
    // The original response should be received by the fake Redis client.
    EXPECT_EQ(response, redis_client->data());

    redis_client->close();
    EXPECT_TRUE(fake_upstream_connection->close());
  }

  void expectCallClusterSlot(int stream_index, std::string& response) {
    std::string cluster_slot_request = makeBulkStringArray({"CLUSTER", "SLOTS"});

    fake_upstreams_[stream_index]->set_allow_unexpected_disconnects(true);

    std::string proxied_cluster_slot_request;

    FakeRawConnectionPtr fake_upstream_connection_;
    EXPECT_TRUE(fake_upstreams_[stream_index]->waitForRawConnection(fake_upstream_connection_));
    EXPECT_TRUE(fake_upstream_connection_->waitForData(cluster_slot_request.size(),
                                                       &proxied_cluster_slot_request));

    EXPECT_EQ(cluster_slot_request, proxied_cluster_slot_request);

    EXPECT_TRUE(fake_upstream_connection_->write(response));
    EXPECT_TRUE(fake_upstream_connection_->close());
  }

  /**
   * Simple response for a single slot redis cluster with a master and slave.
   * @param master the ip of the master node.
   * @param slave the ip of the slave node.
   * @return The cluster slot response.
   */
  std::string singleSlotMasterSlave(const Network::Address::Ip* master,
                                    const Network::Address::Ip* slave) {
    int64_t start_slot = 0;
    int64_t end_slot = 16383;

    std::stringstream resp;
    resp << "*1\r\n"
         << "*4\r\n"
         << ":" << start_slot << "\r\n"
         << ":" << end_slot << "\r\n"
         << makeIp(master->addressAsString(), master->port())
         << makeIp(slave->addressAsString(), slave->port());

    return resp.str();
  }

  /**
   * Simple response for 2 slot redis cluster with 2 nodes.
   * @param slot1 the ip of the master node of slot1.
   * @param slot2 the ip of the master node of slot2.
   * @return The cluster slot response.
   */
  std::string twoSlots(const Network::Address::Ip* slot1, const Network::Address::Ip* slot2) {
    int64_t start_slot1 = 0;
    int64_t end_slot1 = 10000;
    int64_t start_slot2 = 10000;
    int64_t end_slot2 = 16383;

    std::stringstream resp;
    resp << "*2\r\n"
         << "*3\r\n"
         << ":" << start_slot1 << "\r\n"
         << ":" << end_slot1 << "\r\n"
         << makeIp(slot1->addressAsString(), slot1->port()) << "*3\r\n"
         << ":" << start_slot2 << "\r\n"
         << ":" << end_slot2 << "\r\n"
         << makeIp(slot2->addressAsString(), slot2->port());
    return resp.str();
  }

  std::string makeIp(const std::string& address, uint32_t port) {
    return fmt::format("*2\r\n${0}\r\n{1}\r\n:{2}\r\n", address.size(), address, port);
  }

  Runtime::MockRandomGenerator* mock_rng_{};
  const int num_upstreams_;
  const Network::Address::IpVersion version_;
  int random_index_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This test sends a simple "get foo" command from a fake
// downstream client through the proxy to a fake upstream
// Redis cluster with a single slot with master and slave.
// The fake server sends a valid response back to the client.
// The request and response should make it through the envoy
// proxy server code unchanged.

TEST_P(RedisClusterIntegrationTest, SingleSlotMasterSlave) {
  random_index_ = 0;

  on_server_init_function_ = [this]() {
    std::string cluster_slot_response = singleSlotMasterSlave(
        fake_upstreams_[0]->localAddress()->ip(), fake_upstreams_[1]->localAddress()->ip());
    expectCallClusterSlot(0, cluster_slot_response);
  };

  initialize();

  simpleRequestAndResponse(random_index_, makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n");
}

// This test sends a simple "get foo" command from a fake
// downstream client through the proxy to a fake upstream
// Redis cluster with 2 slots. The fake server sends a valid response
// back to the client. The request and response should
// make it through the envoy proxy server code unchanged.

TEST_P(RedisClusterIntegrationTest, TwoSlot) {
  random_index_ = 0;

  on_server_init_function_ = [this]() {
    std::string cluster_slot_response = twoSlots(fake_upstreams_[0]->localAddress()->ip(),
                                                 fake_upstreams_[1]->localAddress()->ip());
    expectCallClusterSlot(0, cluster_slot_response);
  };

  initialize();

  simpleRequestAndResponse(random_index_, makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n");

  // change the load balancer index and hit slot 2 master
  ON_CALL(*mock_rng_, random()).WillByDefault(Return(1));
  simpleRequestAndResponse(1, makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n");
}

} // namespace
} // namespace Envoy
