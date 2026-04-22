#include <chrono>
#include <cstdint>
#include <numeric>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {
namespace {

void configureClusterLoadBalancingPolicy(envoy::config::cluster::v3::Cluster& cluster) {
  auto* policy = cluster.mutable_load_balancing_policy();

  // Configure WRR-Locality LB policy on the cluster level, with a
  // ClientSideWeightedRoundRobinLoadBalancer per-locality LB policy that disables the
  // ClientSideWeightedRoundRobinLoadBalancer out-of-band endpoint reporting. This is because it is
  // currently the only per-locality LB policy that works with WRR-Locality. Note that the field
  // `enable_oob_load_report` isn't being used at the moment in Envoy, making this configuration
  // the default ClientSideWeightedRoundRobinLoadBalancer settings.
  const std::string policy_yaml = R"EOF(
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.wrr_locality
          typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.wrr_locality.v3.WrrLocality
              endpoint_picking_policy:
                policies:
                - typed_extension_config:
                    name: envoy.load_balancing_policies.weighted_round_robin
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin
                        enable_oob_load_report: false
      )EOF";

  TestUtility::loadFromYaml(policy_yaml, *policy);
}

// Testing the WRR-Locality LB policy.
class WrrLocalityIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  WrrLocalityIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream server for 3 different localities.
    setUpstreamCount(kLocalitiesNum);
  }

  void initializeConfig(const std::vector<uint32_t>& localities_weights) {
    // Each upstream will be in its own locality.
    ASSERT(localities_weights.size() == kLocalitiesNum);
    // The upstreams will automatically return 200.
    autonomous_upstream_ = true;
    config_helper_.addConfigModifier(
        [&localities_weights](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
          ASSERT(cluster_0->name() == "cluster_0");

          // Each locality has its own weight and must also have a locality name.
          constexpr absl::string_view locality_yaml = R"EOF(
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          load_balancing_weight: {}
          locality:
            sub_zone: {}
          )EOF";
          const std::string local_address = Network::Test::getLoopbackAddressString(GetParam());

          // Set 3 localities in the cluster, each with its own weight.
          cluster_0->mutable_load_assignment()->clear_endpoints();
          for (uint32_t i = 0; i < kLocalitiesNum; ++i) {
            auto* locality = cluster_0->mutable_load_assignment()->mutable_endpoints()->Add();
            TestUtility::loadFromYaml(fmt::format(locality_yaml, local_address,
                                                  localities_weights[i], absl::StrCat("zone_", i)),
                                      *locality);
          }

          configureClusterLoadBalancingPolicy(*cluster_0);
        });

    HttpIntegrationTest::initialize();
  }

  static constexpr uint32_t kLocalitiesNum = 3;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, WrrLocalityIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that the assigned locality weights are met.
TEST_P(WrrLocalityIntegrationTest, LocalityWeightedRoundRobinLoadBalancing) {
  constexpr uint32_t requests_num = 100;
  const std::vector<uint32_t> localities_weights({50, 30, 20});
  ASSERT(std::accumulate(localities_weights.cbegin(), localities_weights.cend(), 0) ==
         requests_num);
  initializeConfig(localities_weights);

  // Set the response headers of each of the upstreams. Each will add a header
  // "my_upstream_index" that will indicate which upstream server returned
  // the response.
  static const Http::LowerCaseString myUpstreamIndexHeaderName("my_upstream_index");
  // Setup the backends response headers.
  for (uint32_t i = 0; i < kLocalitiesNum; ++i) {
    std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers =
        std::make_unique<Http::TestResponseHeaderMapImpl>(
            Http::TestResponseHeaderMapImpl({{":status", "200"}}));
    response_headers->setCopy(myUpstreamIndexHeaderName, absl::StrCat(i));
    reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[i].get())
        ->setResponseHeaders(std::move(response_headers));
  }

  // Send 100 requests, and validate that the expected distribution meets the
  // expectation (more or less).
  std::vector<uint64_t> upstream_usage({0, 0, 0});
  for (uint32_t i = 0; i < requests_num; ++i) {
    ENVOY_LOG(trace, "Before request {}.", i);
    // Send a request and parse the response.
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "example.com");
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    uint32_t resp_upstream_index = 100; // Intentionally set out of bounds initially.
    ASSERT_TRUE(absl::SimpleAtoi(
        response->headers().get(myUpstreamIndexHeaderName)[0]->value().getStringView(),
        &resp_upstream_index));
    upstream_usage[resp_upstream_index]++;
    cleanupUpstreamAndDownstream();
    ENVOY_LOG(trace, "After request {}.", i);
  }
  // Validate that the expected usage roughly equals to the actual usage.
  for (uint32_t i = 0; i < kLocalitiesNum; ++i) {
    // The expectation that the number of requests per upstream will be up to 2
    // off (because of randomization of the chosen locality order).
    const uint32_t diff =
        std::abs(static_cast<int>(upstream_usage[i]) - static_cast<int>(localities_weights[i]));
    EXPECT_LT(diff, 2) << fmt::format("Unexepected weight for locality {}, expected={}, actual={}",
                                      i, localities_weights[i], upstream_usage[i]);
  }
}

// Tests to verify the behavior of load balancing policy when endpoints are
// updated when using the WrrLocality LB policy for the cluster.
class WrrLocalityEdsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  WrrLocalityEdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam(), config()) {
    use_lds_ = false;
  }

  void initialize() override {
    // The upstreams will automatically return 200.
    autonomous_upstream_ = true;
    // Update the static cluster that is already defined in config() to have a
    // eds_config_source pointing to the eds_helper, and the WrrLocality LB
    // policy.
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          configureClusterLoadBalancingPolicy(*cluster);
          cluster->mutable_common_lb_config()->mutable_update_merge_window()->set_seconds(0);
          cluster->set_type(envoy::config::cluster::v3::Cluster::EDS);
          cluster->mutable_eds_cluster_config()
              ->mutable_eds_config()
              ->mutable_path_config_source()
              ->set_path(eds_helper_.edsPath());
        });
    use_lds_ = false;
    // Set 4 autonomous upstreams, 2 for each locality.
    setUpstreamCount(4);
    setUpstreamProtocol(Http::CodecType::HTTP2);

    // This will be invoked after the test suite will have all the endpoints
    // ports assigned.
    on_server_ready_function_ = [&](IntegrationTestServer&) -> void {
      // Set the EDS response that the EdsHelper will send by default:
      // 2 localities, each with 2 endpoints.
      {
        // Second locality includes fake_upstreams_[0] and fake_upstreams_[1].
        cluster1_endpoints_ = ConfigHelper::buildClusterLoadAssignment(
            first_cluster_name_, Network::Test::getLoopbackAddressString(version_),
            fake_upstreams_[0]->localAddress()->ip()->port());
        cluster1_endpoints_.mutable_endpoints(0)->set_priority(0);
        cluster1_endpoints_.mutable_endpoints(0)->mutable_locality()->set_sub_zone("zone_0");
        auto* address = cluster1_endpoints_.mutable_endpoints(0)
                            ->add_lb_endpoints()
                            ->mutable_endpoint()
                            ->mutable_address()
                            ->mutable_socket_address();
        address->set_address(Network::Test::getLoopbackAddressString(version_));
        address->set_port_value(fake_upstreams_[1]->localAddress()->ip()->port());

        // Second locality includes fake_upstreams_[2] and fake_upstreams_[3].
        auto temp_endpoints = ConfigHelper::buildClusterLoadAssignment(
            first_cluster_name_, Network::Test::getLoopbackAddressString(version_),
            fake_upstreams_[2]->localAddress()->ip()->port());
        temp_endpoints.mutable_endpoints(0)->set_priority(0);
        temp_endpoints.mutable_endpoints(0)->mutable_locality()->set_sub_zone("zone_1");
        auto* address4 = temp_endpoints.mutable_endpoints(0)
                             ->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
        address4->set_address(Network::Test::getLoopbackAddressString(version_));
        address4->set_port_value(fake_upstreams_[3]->localAddress()->ip()->port());
        cluster1_endpoints_.mutable_endpoints()->Add()->MergeFrom(temp_endpoints.endpoints(0));
      }
      // No waiting for the EDS fetching, as the fetching will be done as part of
      // `initialize()`.
      eds_helper_.setEds({cluster1_endpoints_});
    };
    HttpIntegrationTest::initialize();

    // Cluster should become active.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 1);

    // Wait for our statically specified listener to become ready, and register
    // its port in the test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void sendRequestsAndTrackUpstreamUsage(uint64_t number_of_requests,
                                         std::vector<uint64_t>& upstream_usage) {
    // Set the response headers of each of the upstreams. Each will add a header
    // "my_upstream_index" that will indicate which upstream server returned
    // the response.
    static const Http::LowerCaseString myUpstreamIndexHeaderName("my_upstream_index");
    // Setup the backends response headers.
    for (uint32_t i = 0; i < fake_upstreams_.size(); ++i) {
      std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers =
          std::make_unique<Http::TestResponseHeaderMapImpl>(
              Http::TestResponseHeaderMapImpl({{":status", "200"}}));
      response_headers->setCopy(myUpstreamIndexHeaderName, absl::StrCat(i));
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[i].get())
          ->setResponseHeaders(std::move(response_headers));
    }

    // Set the expected number of upstreams.
    upstream_usage.resize(fake_upstreams_.size());
    ENVOY_LOG(trace, "Start sending {} requests.", number_of_requests);

    for (uint64_t i = 0; i < number_of_requests; i++) {
      ENVOY_LOG(trace, "Before request {}.", i);
      // Send a request and parse the response.
      BufferingStreamDecoderPtr response =
          IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/cluster1", "",
                                             downstream_protocol_, version_, "example.com");
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("200", response->headers().getStatusValue());
      uint32_t resp_upstream_index = 100; // Intentionally set out of bounds initially.
      ASSERT_TRUE(absl::SimpleAtoi(
          response->headers().get(myUpstreamIndexHeaderName)[0]->value().getStringView(),
          &resp_upstream_index));
      upstream_usage[resp_upstream_index]++;
      cleanupUpstreamAndDownstream();
      ENVOY_LOG(trace, "After request {}.", i);
    }
  }

  const char* first_cluster_name_ = "cluster_1";

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
      - name: cluster_1
        typed_extension_protocol_options:
          envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
            "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
            explicit_http_config:
              http2_protocol_options: {{}}
      listeners:
      - name: http
        address:
          socket_address:
            address: 127.0.0.1
            port_value: 0
        filter_chains:
          filters:
            name: http
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: config_test
              http_filters:
                name: envoy.filters.http.router
              codec_type: HTTP1
              route_config:
                name: route_config_0
                validate_clusters: false
                virtual_hosts:
                  name: integration
                  routes:
                  - route:
                      cluster: cluster_1
                    match:
                      prefix: "/cluster1"
                  domains: "*"
    )EOF",
                                                    Platform::null_device_path));
  }

  EdsHelper eds_helper_;
  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster1_endpoints_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, WrrLocalityEdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validates that the WrrLocality works as expected after adding a locality, and
// after removing it.
TEST_P(WrrLocalityEdsIntegrationTest, AddRemoveLocality) {
  initialize();
  for (uint32_t i = 0; i < 10; ++i) {
    ENVOY_LOG_MISC(trace, "AddRemoveLocality iteration {}", i);
    // Each iteration will use a different weight for each locality.
    cluster1_endpoints_.mutable_endpoints(0)->mutable_load_balancing_weight()->set_value(i + 1);
    cluster1_endpoints_.mutable_endpoints(1)->mutable_load_balancing_weight()->set_value(i * 3 + 1);
    envoy::config::endpoint::v3::ClusterLoadAssignment current_endpoints;
    bool use_single_locality = (i % 2 != 0);
    current_endpoints = cluster1_endpoints_;
    if (use_single_locality) {
      current_endpoints.mutable_endpoints()->DeleteSubrange(1, 1);
    }
    eds_helper_.setEds({current_endpoints});
    test_server_->waitForCounterGe("cluster.cluster_1.membership_change", i + 1);

    const std::vector<uint64_t> upstream_qps = {100, 100, 100, 100};
    // Send another 100 requests to cluster1, expecting weights to be used.
    std::vector<uint64_t> upstream_usage;
    sendRequestsAndTrackUpstreamUsage(100, upstream_usage);
    ENVOY_LOG(trace, "upstream_usage {}", upstream_usage);
    // Expect the usage of first locality to be non-zero.
    EXPECT_GT(upstream_usage[0], 0);
    EXPECT_GT(upstream_usage[1], 0);
    // Expect the usage of second locality to be non-zero if the priority is
    // not set to 1.
    if (use_single_locality) {
      EXPECT_EQ(upstream_usage[2], 0);
      EXPECT_EQ(upstream_usage[3], 0);
    } else {
      EXPECT_GT(upstream_usage[2], 0);
      EXPECT_GT(upstream_usage[3], 0);
    }
  }
}
} // namespace
} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
