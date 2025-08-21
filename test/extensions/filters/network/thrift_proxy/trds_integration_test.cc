#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"

#include "source/common/common/fmt.h"

#include "test/extensions/filters/network/thrift_proxy/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftTrdsIntegrationTest : public testing::Test, public BaseThriftIntegrationTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    thrift_config_ = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  ads_config:
    api_type: GRPC
    grpc_services:
      envoy_grpc:
        cluster_name: xds_cluster
static_resources:
  clusters:
  - name: xds_cluster
    load_assignment:
      cluster_name: xds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: cluster_1
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: cluster_2
    load_assignment:
      cluster_name: cluster_2
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        - name: thrift
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.ThriftProxy
            stat_prefix: thrift_stats
            trds:
              config_source:
                ads: {{}}
              route_config_name: test_route

)EOF",
                                 Platform::null_device_path);
  }

  void initialize() override {
    setUpstreamCount(3);

    PayloadOptions options(TransportType::Framed, ProtocolType::Binary, DriverMode::Success,
                           absl::optional<std::string>(), "poke");
    preparePayloads(options, request_bytes_, response_bytes_);

    BaseThriftIntegrationTest::initialize();

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_));
    ASSERT_TRUE(xds_connection_->waitForNewStream(*dispatcher_, xds_stream_));
    xds_stream_->startGrpcStream();
    EXPECT_TRUE(compareSotwDiscoveryRequest(""));

    test_server_->waitUntilListenersReady();
  }

  void sendStowDiscoveryResponse(const std::string& config, const std::string& version) {
    BaseThriftIntegrationTest::sendSotwDiscoveryResponse<RouteConfiguration>(
        Config::getTypeUrl<RouteConfiguration>(),
        {TestUtility::parseYaml<RouteConfiguration>(config)}, version);
    EXPECT_TRUE(compareSotwDiscoveryRequest(version));
  }

  AssertionResult compareSotwDiscoveryRequest(const std::string& version) {
    return BaseThriftIntegrationTest::compareSotwDiscoveryRequest(
        Config::getTypeUrl<RouteConfiguration>(), version, {"test_route"}, true);
  }

  using RouteConfiguration =
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration;

  Buffer::OwnedImpl request_bytes_;
  Buffer::OwnedImpl response_bytes_;
};

TEST_F(ThriftTrdsIntegrationTest, Basic) {
  initialize();

  constexpr absl::string_view test_route = R"EOF(
name: test_route
routes:
  - match:
      method_name: "poke"
    route:
      cluster: "cluster_{}"
)EOF";

  sendStowDiscoveryResponse(fmt::format(test_route, 1), "1");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(request_bytes_.toString()));

  FakeRawConnectionPtr fake_upstream_connection;
  std::string data;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(data), request_bytes_));

  sendStowDiscoveryResponse(fmt::format(test_route, 2), "2");

  ASSERT_TRUE(tcp_client->write(request_bytes_.toString()));

  ASSERT_TRUE(fake_upstreams_[2]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request_bytes_.length(), &data));
  EXPECT_TRUE(TestUtility::buffersEqual(Buffer::OwnedImpl(data), request_bytes_));

  tcp_client->close();

  ASSERT_EQ(2U, test_server_->counter("thrift.thrift_stats.request_oneway")->value());
  ASSERT_EQ(0U, test_server_->counter("thrift.thrift_stats.route_missing")->value());
  ASSERT_EQ(1U, test_server_->counter("cluster.cluster_1.thrift.upstream_rq_oneway")->value());
  ASSERT_EQ(1U, test_server_->counter("cluster.cluster_2.thrift.upstream_rq_oneway")->value());

  cleanUpXdsConnection();
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
