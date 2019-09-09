#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

const std::string SUBSET_CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    lb_policy: round_robin
    lb_subset_config:
      subset_selectors:
        - keys: [ "type" ]
    load_assignment:
      cluster_name: cluster_0
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 0
            metadata:
              filter_metadata:
                "envoy.lb": { "type": "a" }
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 0
            metadata:
              filter_metadata:
                "envoy.lb": { "type": "a" }
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 0
            metadata:
              filter_metadata:
                "envoy.lb": { "type": "b" }
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 0
            metadata:
              filter_metadata:
                "envoy.lb": { "type": "b" }
  listeners:
    name: listener_0
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
          codec_type: HTTP1
          access_log:
            name: envoy.file_access_log
            filter:
              not_health_check_filter:  {}
            config:
              path: /dev/null
          route_config:
            name: route_config_0
            virtual_hosts:
              name: integration
              domains: "*"
              routes:
                - match:
                    prefix: "/"
                    headers:
                      - name: "x-type"
                        exact_match: "a"
                  route:
                    cluster: cluster_0
                    metadata_match:
                      filter_metadata:
                        "envoy.lb": { "type": "a" }
                    hash_policy:
                      - header:
                          header_name: "x-hash"
                - match:
                    prefix: "/"
                    headers:
                      - name: "x-type"
                        exact_match: "b"
                  route:
                    cluster: cluster_0
                    metadata_match:
                      filter_metadata:
                        "envoy.lb": { "type": "b" }
                    hash_policy:
                      - header:
                          header_name: "x-hash"
              response_headers_to_add:
                - header:
                    key: "x-host-type"
                    value: '%UPSTREAM_METADATA(["envoy.lb", "type"])%'
                - header:
                    key: "x-host"
                    value: '%UPSTREAM_REMOTE_ADDRESS%'
)EOF";

} // namespace
class HttpSubsetLbIntegrationTest : public testing::TestWithParam<envoy::api::v2::Cluster_LbPolicy>,
                                    public HttpIntegrationTest {
public:
  // Returns all load balancer types except ORIGINAL_DST_LB and CLUSTER_PROVIDED.
  static std::vector<envoy::api::v2::Cluster_LbPolicy> getSubsetLbTestParams() {
    int first = static_cast<int>(envoy::api::v2::Cluster_LbPolicy_LbPolicy_MIN);
    int last = static_cast<int>(envoy::api::v2::Cluster_LbPolicy_LbPolicy_MAX);
    ASSERT(first < last);

    std::vector<envoy::api::v2::Cluster_LbPolicy> ret;
    for (int i = first; i <= last; i++) {
      if (!envoy::api::v2::Cluster_LbPolicy_IsValid(i)) {
        continue;
      }

      auto policy = static_cast<envoy::api::v2::Cluster_LbPolicy>(i);

      if (policy == envoy::api::v2::Cluster_LbPolicy_ORIGINAL_DST_LB ||
          policy == envoy::api::v2::Cluster_LbPolicy_CLUSTER_PROVIDED ||
          policy == envoy::api::v2::Cluster_LbPolicy_LOAD_BALANCING_POLICY_CONFIG) {
        continue;
      }

      ret.push_back(policy);
    }

    return ret;
  }

  // Converts an LbPolicy to strings suitable for test names.
  static std::string
  subsetLbTestParamsToString(const testing::TestParamInfo<envoy::api::v2::Cluster_LbPolicy>& p) {
    const std::string& policy_name = envoy::api::v2::Cluster_LbPolicy_Name(p.param);
    return absl::StrReplaceAll(policy_name, {{"_", ""}});
  }

  HttpSubsetLbIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, Network::Address::IpVersion::v4,
                            SUBSET_CONFIG) {
    autonomous_upstream_ = true;
    setUpstreamCount(4);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      for (int i = 0; i < static_resources->clusters_size(); ++i) {
        auto* cluster = static_resources->mutable_clusters(i);
        cluster->set_lb_policy(GetParam());
      }
    });
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  // Runs a subset lb test with the given request headers, expecting the x-host-type header to
  // the given type ("a" or "b"). If expect_unique_host, verifies that a single host is selected
  // over n iterations (e.g. for maglev/hash-ring policies). Otherwise, expected more than one
  // host to be selected over n iterations.
  void runTest(Http::TestHeaderMapImpl& request_headers, const std::string expected_host_type,
               const bool expect_unique_host, const int n = 10) {
    std::set<std::string> hosts;
    for (int i = 0; i < n; i++) {
      Http::TestHeaderMapImpl response_headers{{":status", "200"}};

      // Send header only request.
      IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
      response->waitForEndStream();

      // Expect a response from a host in the correct subset.
      EXPECT_EQ(response->headers()
                    .get(Envoy::Http::LowerCaseString{"x-host-type"})
                    ->value()
                    .getStringView(),
                expected_host_type);

      hosts.emplace(
          response->headers().get(Envoy::Http::LowerCaseString{"x-host"})->value().getStringView());
    }

    if (expect_unique_host) {
      EXPECT_EQ(hosts.size(), 1) << "Expected a single unique host to be selected for "
                                 << envoy::api::v2::Cluster_LbPolicy_Name(GetParam());
    } else {
      EXPECT_GT(hosts.size(), 1) << "Expected multiple hosts to be selected"
                                 << envoy::api::v2::Cluster_LbPolicy_Name(GetParam());
    }
  }

  Http::TestHeaderMapImpl type_a_request_headers_{{":method", "GET"},  {":path", "/test"},
                                                  {":scheme", "http"}, {":authority", "host"},
                                                  {"x-type", "a"},     {"x-hash", "hash-a"}};
  Http::TestHeaderMapImpl type_b_request_headers_{{":method", "GET"},  {":path", "/test"},
                                                  {":scheme", "http"}, {":authority", "host"},
                                                  {"x-type", "b"},     {"x-hash", "hash-b"}};
};

INSTANTIATE_TEST_SUITE_P(SubsetCompatibleLoadBalancers, HttpSubsetLbIntegrationTest,
                         testing::ValuesIn(HttpSubsetLbIntegrationTest::getSubsetLbTestParams()),
                         HttpSubsetLbIntegrationTest::subsetLbTestParamsToString);

// Tests each subset-compatible load balancer policy with 4 hosts divided into 2 subsets.
TEST_P(HttpSubsetLbIntegrationTest, SubsetLoadBalancer) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  const bool expect_unique_host = (GetParam() == envoy::api::v2::Cluster_LbPolicy_RING_HASH ||
                                   GetParam() == envoy::api::v2::Cluster_LbPolicy_MAGLEV);

  runTest(type_a_request_headers_, "a", expect_unique_host);
  runTest(type_b_request_headers_, "b", expect_unique_host);
}

} // namespace Envoy
