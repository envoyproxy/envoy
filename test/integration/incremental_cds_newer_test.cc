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
      api_type: INCREMENTAL_GRPC
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
)EOF";
const std::string kClusterName = "cluster_0";

class CdsIntegrationTest : public XdsIntegrationTestBase,
                           public Grpc::GrpcClientIntegrationParamTest,
                           public Envoy::Upstream::ClusterUpdateCallbacks {
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
  
  AssertionResult
  compareDiscoveryRequest(const std::string& expected_type_url,
                          const std::vector<std::string>& expected_resource_subscriptions,
                          const std::vector<std::string>& expected_resource_unsubscriptions,
                          const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
                          const std::string& expected_error_message = "") {
    envoy::api::v2::IncrementalDiscoveryRequest request;
    VERIFY_ASSERTION(cds_stream_->waitForGrpcMessage(*dispatcher_, request));

    EXPECT_TRUE(request.has_node());
    EXPECT_FALSE(request.node().id().empty());
    EXPECT_FALSE(request.node().cluster().empty());

    // TODO(PiotrSikora): Remove this hack once fixed internally.
    if (!(expected_type_url == request.type_url())) {
      return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                               request.type_url(), expected_type_url);
    }
    if (!(expected_error_code == request.error_detail().code())) {
      return AssertionFailure() << fmt::format("error_code {} does not match expected {}",
                                               request.error_detail().code(),
                                               expected_error_code);
    }
    EXPECT_TRUE(
        IsSubstring("", "", expected_error_message, request.error_detail().message()));

    const std::vector<std::string> resource_subscriptions(
        request.resource_names_subscribe().cbegin(), request.resource_names_subscribe().cend());
    if (expected_resource_subscriptions != resource_subscriptions) {
      return AssertionFailure() << fmt::format(
                 "newly subscribed resources {} do not match expected {} in {}",
                 fmt::join(resource_subscriptions.begin(), resource_subscriptions.end(), ","),
                 fmt::join(expected_resource_subscriptions.begin(),
                           expected_resource_subscriptions.end(), ","),
                 request.DebugString());
    }
    const std::vector<std::string> resource_unsubscriptions(
        request.resource_names_unsubscribe().cbegin(), request.resource_names_unsubscribe().cend());
    if (expected_resource_unsubscriptions != resource_unsubscriptions) {
      return AssertionFailure() << fmt::format(
                 "newly UNsubscribed resources {} do not match expected {} in {}",
                 fmt::join(resource_unsubscriptions.begin(), resource_unsubscriptions.end(), ","),
                 fmt::join(expected_resource_unsubscriptions.begin(),
                           expected_resource_unsubscriptions.end(), ","),
                 request.DebugString());
    }
    return AssertionSuccess();
  }
  
  template <class T>
  void sendDiscoveryResponse(const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed,
                             const std::string& version) {
    envoy::api::v2::IncrementalDiscoveryResponse response;
    response.set_system_version_info("system_version_info_this_is_a_test");
    for (const auto& message : added_or_updated) {
      auto* resource = response.add_resources();
      resource->set_version(version);
      resource->mutable_resource()->PackFrom(message);
    }
    *response.mutable_removed_resources() = {removed.begin(), removed.end()};
    response.set_nonce("noncense");
    cds_stream_->sendGrpcMessage(response);
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
                    // fake_upstreams_[1] is the CDS server, [0] is the regular upstream.
                    fake_upstreams_[0]->localAddress()->ip()->port()));
  }

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& cluster) {
    return TestUtility::parseYaml<envoy::api::v2::Listener>(fmt::format(
        R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
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
                    cluster: {}
                  match:
                    prefix: "/"
                domains: "*"
              name: route_config_0
    )EOF",
        name, Network::Test::getLoopbackAddressString(ipVersion()), cluster));
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
    //    the bootstrap config have come up, and registering them in a port map (see lookupPort()).
    //    However, this test needs to defer all of that to later.
    XdsIntegrationTestBase::initialize();

    // Create the regular (i.e. not an xDS server) upstream. We create it manually here after
    // initialize() because finalize() expects all fake_upstreams_ to correspond to a static
    // cluster in the bootstrap config - which we don't want since we're testing dynamic CDS!
    // HACK: In order to use the HttpIntegrationTest cases unmodified,
    //       HttpIntegrationTest::waitForNextUpstreamRequest() needs to find the regular upstream
    //       in the 0th position.
    fake_upstreams_.push_back(std::move(fake_upstreams_[0]));
    fake_upstreams_[0] = std::make_unique<FakeUpstream>(0, FakeHttpConnection::Type::HTTP2,
                                                        version_, timeSystem(), enable_half_close_);
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

    // These ClusterManager update callbacks are involved in spooky thread-local storage stuff, and
    // therefore their installation must be scheduled by post() for some reason.
    Upstream::ClusterUpdateCallbacksHandlePtr cb_handle;
    ConditionalInitializer cb_added;
    test_server_->server().dispatcher().post([this, &cb_added, &cb_handle]() -> void {
      // Prepare to be notified by the cluster manager that it processed our upcoming CDS response.
      cb_handle =
          test_server_->server().clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
      cb_added.setReady();
    });
    cb_added.waitReady();

    // Now that the upstream has been created, process Envoy's request to discover it.
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    
    
    
    // TODO TODO STUFF HERE!!!!!!!!!!
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, {""}, {""}));
    sendDiscoveryResponse<envoy::api::v2::Cluster>({buildCluster("cluster_0")},
                                                   {"removed_1", "removed_2"}, "1");
    
    
    fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
    
    
    
    
    

    // We can continue the test once we're sure that Envoy's ClusterManager has made use of
    // the DiscoveryResponse describing cluster_0 that we sent.
    cluster_added_by_cm_.waitReady();

    // Manually create a listener, add it to the cluster manager, register its port in the
    // test framework's downstream listener port map, and finally wait for it to start listening.
    //
    // Why do all of this manually? Because it's unworkable to specify this listener statically in
    // the bootstrap config. Its route is meant to point to the regular data upstream. To specificy
    // that route statically, the regular upstream (cluster_0) would have to be specified
    // statically - but the whole point of this test is to have Envoy dynamically discover it!
    ConditionalInitializer listener_added_by_worker;
    ConditionalInitializer listener_added_by_manager;
    test_server_->setOnWorkerListenerAddedCb(
        [&listener_added_by_worker]() -> void { listener_added_by_worker.setReady(); });
    test_server_->server().dispatcher().post([this, &listener_added_by_manager]() -> void {
      EXPECT_TRUE(test_server_->server().listenerManager().addOrUpdateListener(
          buildListener("http", kClusterName), "", true));
      listener_added_by_manager.setReady();
    });
    listener_added_by_worker.waitReady();
    listener_added_by_manager.waitReady();

    registerTestServerPorts({"http"});
    test_server_->waitUntilListenersReady();
  }

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(Envoy::Upstream::ThreadLocalCluster& cluster) override {
    if (cluster.info()->name() == kClusterName) {
      cluster_added_by_cm_.setReady();
    }
  }
  void onClusterRemoval(const std::string&) override {}
  ConditionalInitializer cluster_added_by_cm_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, CdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(CdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
  cleanupUpstreamAndDownstream();
  fake_upstream_connection_ = nullptr;
}

} // namespace
} // namespace Envoy
