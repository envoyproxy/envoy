#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

class HttpSubsetLbIntegrationTest
    : public testing::TestWithParam<envoy::config::cluster::v3::Cluster::LbPolicy>,
      public HttpIntegrationTest {
public:
  // Returns all load balancer types except ORIGINAL_DST_LB, CLUSTER_PROVIDED
  // and LOAD_BALANCING_POLICY_CONFIG.
  static std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> getSubsetLbTestParams() {
    int first = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MIN);
    int last = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MAX);
    ASSERT(first < last);

    std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> ret;
    for (int i = first; i <= last; i++) {
      if (!envoy::config::cluster::v3::Cluster::LbPolicy_IsValid(i)) {
        continue;
      }

      auto policy = static_cast<envoy::config::cluster::v3::Cluster::LbPolicy>(i);

      if (policy == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED ||
          policy == envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG) {
        continue;
      }

      ret.push_back(policy);
    }

    return ret;
  }

  // Converts an LbPolicy to strings suitable for test names.
  static std::string subsetLbTestParamsToString(
      const testing::TestParamInfo<envoy::config::cluster::v3::Cluster::LbPolicy>& p) {
    const std::string& policy_name = envoy::config::cluster::v3::Cluster::LbPolicy_Name(p.param);
    return absl::StrReplaceAll(policy_name, {{"_", ""}});
  }

  HttpSubsetLbIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()),
        is_hash_lb_(GetParam() == envoy::config::cluster::v3::Cluster::RING_HASH ||
                    GetParam() == envoy::config::cluster::v3::Cluster::MAGLEV) {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      cluster->set_lb_policy(GetParam());

      // Create subsets based on type value of the "type" metadata.
      cluster->mutable_lb_subset_config()->add_subset_selectors()->add_keys(type_key_);

      cluster->clear_load_assignment();

      // Create a load assignment with num_hosts_ entries with metadata split evenly between
      // type=a and type=b.
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      for (uint32_t i = 0; i < num_hosts_; i++) {
        auto* lb_endpoint = endpoints->add_lb_endpoints();

        // ConfigHelper will fill in ports later.
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* addr = endpoint->mutable_address()->mutable_socket_address();
        addr->set_address(Network::Test::getLoopbackAddressString(
            TestEnvironment::getIpVersionsForTest().front()));
        addr->set_port_value(0);

        // Assign type metadata based on i.
        auto* metadata = lb_endpoint->mutable_metadata();
        Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", type_key_)
            .set_string_value((i % 2 == 0) ? "a" : "b");
      }
    });

    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);

          // Report the host's type metadata and remote address on every response.
          auto* resp_header = vhost->add_response_headers_to_add();
          auto* header = resp_header->mutable_header();
          header->set_key(host_type_header_);
          header->set_value(
              fmt::format(R"EOF(%UPSTREAM_METADATA(["envoy.lb", "{}"])%)EOF", type_key_));

          resp_header = vhost->add_response_headers_to_add();
          header = resp_header->mutable_header();
          header->set_key(host_header_);
          header->set_value("%UPSTREAM_REMOTE_ADDRESS%");

          // Create routes for x-type=a and x-type=b headers.
          vhost->clear_routes();
          configureRoute(vhost->add_routes(), "a");
          configureRoute(vhost->add_routes(), "b");
        });
  }

  void configureRoute(envoy::config::route::v3::Route* route, const std::string& host_type) {
    auto* match = route->mutable_match();
    match->set_prefix("/");

    // Match the x-type header against the given host_type (a/b).
    auto* match_header = match->add_headers();
    match_header->set_name(type_header_);
    match_header->mutable_string_match()->set_exact(host_type);

    // Route to cluster_0, selecting metadata type=a or type=b.
    auto* action = route->mutable_route();
    action->set_cluster("cluster_0");
    auto* metadata_match = action->mutable_metadata_match();
    Envoy::Config::Metadata::mutableMetadataValue(*metadata_match, "envoy.lb", type_key_)
        .set_string_value(host_type);

    // Set a hash policy for hashing load balancers.
    if (is_hash_lb_) {
      action->add_hash_policy()->mutable_header()->set_header_name(hash_header_);
    }
  };

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  // Runs a subset lb test with the given request headers, expecting the x-host-type header to
  // the given type ("a" or "b"). If is_hash_lb_, verifies that a single host is selected over n
  // iterations (e.g. for maglev/hash-ring policies). Otherwise, expected more than one host to be
  // selected over at least n iterations and at most m.
  void runTest(Http::TestRequestHeaderMapImpl& request_headers,
               const std::string expected_host_type, const int n = 100, const int m = 1000) {
    ASSERT_LT(n, m);

    std::set<std::string> hosts;
    for (int i = 0; i < m; i++) {
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

      // Send header only request.
      IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
      ASSERT_TRUE(response->waitForEndStream());

      // Expect a response from a host in the correct subset.
      EXPECT_EQ(response->headers()
                    .get(Envoy::Http::LowerCaseString{host_type_header_})[0]
                    ->value()
                    .getStringView(),
                expected_host_type);

      // Record the upstream address.
      hosts.emplace(response->headers()
                        .get(Envoy::Http::LowerCaseString{host_header_})[0]
                        ->value()
                        .getStringView());

      if (i >= n && (is_hash_lb_ || hosts.size() > 1)) {
        // Once we've completed n iterations, quit for hash lb policies. For others, keep going
        // until we've seen multiple hosts (as expected) or reached m iterations.
        break;
      }
    }

    if (is_hash_lb_) {
      EXPECT_EQ(hosts.size(), 1) << "Expected a single unique host to be selected for "
                                 << envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam());
    } else {
      EXPECT_GT(hosts.size(), 1) << "Expected multiple hosts to be selected for "
                                 << envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam());
    }
  }

  struct EndpointConfig {
    envoy::config::core::v3::HealthStatus health_status;
    uint32_t weight;
  };

  // Runs a subset lb test to verify traffic correctly spills over across priorities. Two
  // priorities are created: the first three hosts are added to p0, and the fourth host is added to
  // p1. The provided endpoint config determines the health and weight of each endpoint. This
  // config, along with the provided overprovisioning factor and weighted priority health, can be
  // used to test various spillover behavior.
  void runSpilloverTest(std::vector<EndpointConfig> endpoint_config,
                        uint32_t overprovisioning_factor, bool weighted_priority_health,
                        uint32_t expected_host_count) {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ASSERT(endpoint_config.size() == 4);

      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      // We specifically don't want to use a load balancing policy that does consistent
      // hashing because we want our requests to be load balanced across all hosts.
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

      cluster->mutable_lb_subset_config()->set_fallback_policy(
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT);

      cluster->clear_load_assignment();

      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* policy = load_assignment->mutable_policy();
      policy->mutable_overprovisioning_factor()->set_value(overprovisioning_factor);
      policy->set_weighted_priority_health(weighted_priority_health);
      for (uint32_t i = 0; i < 2; i++) {
        auto* endpoints = load_assignment->add_endpoints();
        endpoints->set_priority(i);
      }

      for (uint32_t i = 0; i < num_hosts_; i++) {
        uint32_t priority;
        if (i < num_hosts_ - 1) {
          priority = 0;
        } else {
          priority = 1;
        }

        auto* endpoints = load_assignment->mutable_endpoints(priority);
        auto* lb_endpoint = endpoints->add_lb_endpoints();
        lb_endpoint->set_health_status(endpoint_config[i].health_status);
        lb_endpoint->mutable_load_balancing_weight()->set_value(endpoint_config[i].weight);

        // ConfigHelper will fill in ports later.
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* addr = endpoint->mutable_address()->mutable_socket_address();
        addr->set_address(Network::Test::getLoopbackAddressString(
            TestEnvironment::getIpVersionsForTest().front()));
        addr->set_port_value(0);
      }
    });

    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    std::set<std::string> hosts;
    for (int i = 0; i < 100; i++) {
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

      // Send header only request.
      auto response = codec_client_->makeHeaderOnlyRequest(type_a_request_headers_);
      ASSERT_TRUE(response->waitForEndStream());

      // Record the upstream address.
      hosts.emplace(response->headers()
                        .get(Envoy::Http::LowerCaseString{host_header_})[0]
                        ->value()
                        .getStringView());
    }

    EXPECT_EQ(hosts.size(), expected_host_count);
  }

  const uint32_t num_hosts_{4};
  const bool is_hash_lb_;

  const std::string hash_header_{"x-hash"};
  const std::string host_type_header_{"x-host-type"};
  const std::string host_header_{"x-host"};
  const std::string type_header_{"x-type"};
  const std::string type_key_{"type"};

  Http::TestRequestHeaderMapImpl type_a_request_headers_{
      {":method", "GET"},     {":path", "/test"}, {":scheme", "http"},
      {":authority", "host"}, {"x-type", "a"},    {"x-hash", "hash-a"}};
  Http::TestRequestHeaderMapImpl type_b_request_headers_{
      {":method", "GET"},     {":path", "/test"}, {":scheme", "http"},
      {":authority", "host"}, {"x-type", "b"},    {"x-hash", "hash-b"}};
};

INSTANTIATE_TEST_SUITE_P(SubsetCompatibleLoadBalancers, HttpSubsetLbIntegrationTest,
                         testing::ValuesIn(HttpSubsetLbIntegrationTest::getSubsetLbTestParams()),
                         HttpSubsetLbIntegrationTest::subsetLbTestParamsToString);

// Tests each subset-compatible load balancer policy with 4 hosts divided into 2 subsets.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancer) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  runTest(type_a_request_headers_, "a");
  runTest(type_b_request_headers_, "b");
}

// Tests subset-compatible load balancer policy without metadata does not crash on initialization
// with single_host_per_subset set to be true.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancerSingleHostPerSubsetNoMetadata) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    // Set single_host_per_subset to be true. This will avoid function
    // SubsetLoadBalancer::rebuildSingle() bailout early. Thus exercise the no metadata logic.
    auto* subset_selector = cluster->mutable_lb_subset_config()->mutable_subset_selectors(0);
    subset_selector->set_single_host_per_subset(true);

    // Clear the metadata for each host
    auto* load_assignment = cluster->mutable_load_assignment();
    auto* endpoints = load_assignment->mutable_endpoints(0);
    for (uint32_t i = 0; i < num_hosts_; i++) {
      auto* lb_endpoint = endpoints->mutable_lb_endpoints(i);
      lb_endpoint->clear_metadata();
    }
  });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                          {":path", "/test"},
                                                                          {":scheme", "http"},
                                                                          {":authority", "host"},
                                                                          {"x-type", "a"},
                                                                          {"x-hash", "hash-a"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));
}

// Tests that overriding the overprovisioning factor works as expected.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancerOverrideOverprovisioningFactor) {
  std::vector<EndpointConfig> endpoint_config;
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 1});

  // Even though 2/3 hosts in p0 are unhealthy, the overprovisioning factor value is so
  // high that traffic won't spill over into p1.
  runSpilloverTest(endpoint_config, 9999, false, 1);
}

// Tests behavior when weighted priority health is disabled.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancerWeightedPriorityHealthDisabled) {
  std::vector<EndpointConfig> endpoint_config;
  // Since 2/3 hosts in p0 are unhealthy, traffic will spill over into p1.
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 6});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 1});

  runSpilloverTest(endpoint_config, 140, false, 2);
}

// Tests behavior when weighted priority health is enabled.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancerWeightedPriorityHealthEnabled) {
  std::vector<EndpointConfig> endpoint_config;
  // Even though 2/3 hosts in p0 are unhealthy, traffic will not spill over into p1 due
  // to the weight of the healthy host in p0.
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 6});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::UNHEALTHY, 1});
  endpoint_config.push_back({envoy::config::core::v3::HEALTHY, 1});

  runSpilloverTest(endpoint_config, 140, true, 1);
}

} // namespace Envoy
