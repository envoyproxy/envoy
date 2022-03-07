#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/tcp_proxy/tcp_proxy.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

const std::string& config() {
  CONSTRUCT_ON_FIRST_USE(std::string, fmt::format(R"EOF(
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
static_resources:
  clusters:
  - name: xds_cluster
    type: STATIC
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: xds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
  - name: tcp_proxy
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
    - filters:
      - name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          cluster: new_cluster
          stat_prefix: on_demand_tcp_proxy
          on_demand:
            odcds_config:
              resource_api_version: V3
              api_config_source:
                api_type: DELTA_GRPC
                transport_api_version: V3
                grpc_services:
                  envoy_grpc:
                    cluster_name: xds_cluster
)EOF",
                                                  Platform::null_device_path));
}

class TcpProxyOdcdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                     public BaseIntegrationTest {
public:
  TcpProxyOdcdsIntegrationTest() : BaseIntegrationTest(std::get<0>(GetParam()), config()) {
    // The test envoy use static listener and cluster for xDS.
    // The test framework does not update the above static resources.
    // Another upstream will be serving delta CDS reqeuests and the response is explicitly generated
    // by test case rather than the integration test framework.
    use_lds_ = false;
    enableHalfClose(true);
  }
  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    // The first upstream serves the ADS request including the future on demand CDS request.
    setUpstreamCount(1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    BaseIntegrationTest::initialize();

    // HTTP protocol version is not used because tcp stream is expected.
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_.back()->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"tcp_proxy"});
  }

  // The on demand CDS stream. The requested cluster config is delivered to Envoy in this stream.
  FakeStreamPtr odcds_stream_;
  // The prepared cluster config response.
  envoy::config::cluster::v3::Cluster new_cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, TcpProxyOdcdsIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(TcpProxyOdcdsIntegrationTest, Basic) {
  initialize();

  // Establish a tcp request to the Envoy.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond with the prepared ``new_cluster``.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  // This upstream is listening on the endpoint of ``new_cluster``. It starts to serve tcp_proxy.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_.back()->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");

  ASSERT_TRUE(tcp_client->write("world"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

} // namespace
} // namespace Envoy
