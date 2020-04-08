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
  // Returns all load balancer types except ORIGINAL_DST_LB and CLUSTER_PROVIDED.
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

      if (policy == envoy::config::cluster::v3::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB ||
          policy == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED ||
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
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()),
        num_hosts_{4}, is_hash_lb_(GetParam() == envoy::config::cluster::v3::Cluster::RING_HASH ||
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
    match_header->set_exact_match(host_type);

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
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
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
      response->waitForEndStream();

      // Expect a response from a host in the correct subset.
      EXPECT_EQ(response->headers()
                    .get(Envoy::Http::LowerCaseString{host_type_header_})
                    ->value()
                    .getStringView(),
                expected_host_type);

      // Record the upstream address.
      hosts.emplace(response->headers()
                        .get(Envoy::Http::LowerCaseString{host_header_})
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

  const uint32_t num_hosts_;
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

} // namespace Envoy
