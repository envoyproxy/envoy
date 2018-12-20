#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/utility.h"
#include "test/integration/xds_integration_test_base.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::IsSubstring;

namespace Envoy {
namespace {

// NOTE: it is required that the listener be named 'http'! Down in the various guts that do the
//       bidding of BaseIntegrationTest::createEnvoy(), a list of service names - taken from the
//       names of the listeners - is registered into a listener (i.e. downstream-facing) port map.
//       The various HttpIntegrationTest tests expect to look up in that port map a service called
//       'http'.
const std::string kConfig = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  cds_config:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: my_cds_cluster
static_resources:
  clusters:
  - name: my_cds_cluster
    http2_protocol_options: {}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
  - name: http
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        config:
          stat_prefix: config_test
          http_filters:
            name: envoy.router
          codec_type: HTTP2
          route_config:
            virtual_hosts:
              name: integration
              routes:
              - route:
                  cluster: my_cds_cluster
                match:
                  prefix: "/"
              domains: "*"
            name: route_config_0
)EOF";

class CdsIntegrationTest : public XdsIntegrationTestBase,
                           public Grpc::GrpcClientIntegrationParamTest {
public:
  CdsIntegrationTest()
      : XdsIntegrationTestBase(Http::CodecClient::Type::HTTP2, ipVersion(), kConfig) {}

  void TearDown() override {
    AssertionResult result = xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    xds_connection_.reset();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  envoy::api::v2::Cluster buildCluster(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::Cluster>(
        fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: STATIC
      load_assignment:
        cluster_name: {}
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: {}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                    name, name, Network::Test::getLoopbackAddressString(ipVersion()),
                    fake_upstreams_[1]->localAddress()->ip()->port()));
  }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many fake_upstreams_.emplace_back(new FakeUpstream) will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(1);                                  // the CDS cluster
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2); // CDS uses gRPC uses HTTP2.

    // BaseIntegrationTest::initialize() does many things:
    // 1) It appends to fake_upstreams_ as many as you asked for via setUpstreamCount().
    // 2) It updates your bootstrap config with the ports your fake upstreams are actually listening
    //    on (since you're supposed to leave them as 0).
    // 3) It creates and starts an IntegrationTestServer - the thing that wraps the almost-actual
    //    Envoy used in the tests.
    // 4) Bringing up the server usually entails waiting to ensure that any listeners specified in
    //    the bootstrap config have come up. However, you might need to defer that to later, which
    //    this test does (using defer_listener_wait_ and waitUntilListenersReady()).
    // 5) It registers those listeners' names+ports in a port map, to be retrieved by lookupPort().
    defer_listener_wait_ = true;
    XdsIntegrationTestBase::initialize();

    // Create the regular (i.e. not an xDS server) upstream. We create it manually here after
    // initialize() because finalize() expects all fake_upstreams_ to correspond to a static
    // cluster in the bootstrap config - which we don't want since we're testing dynamic CDS!
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_,
                                                  timeSystem(), enable_half_close_));

    // Causes xds_connection_ to be filled with a newly constructed FakeHttpConnection.
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
    sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                   {buildCluster("cluster_0")}, "1");
    test_server_->waitUntilListenersReady();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, CdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(CdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
  cleanupUpstreamAndDownstream();
  fake_upstream_connection_ = nullptr;
}

} // namespace
} // namespace Envoy
