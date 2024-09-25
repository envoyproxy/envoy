#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClientSideWeightedRoundRobin {
namespace {

class ClientSideWeightedRoundRobinIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClientSideWeightedRoundRobinIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream server for stateful session test.
    setUpstreamCount(3);
  }

  void initializeConfig() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      constexpr absl::string_view endpoints_yaml = R"EOF(
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
          )EOF";

      const std::string local_address = Network::Test::getLoopbackAddressString(GetParam());
      TestUtility::loadFromYaml(
          fmt::format(endpoints_yaml, local_address, local_address, local_address), *endpoint);

      auto* policy = cluster_0->mutable_load_balancing_policy();

      // Configure LB policy with short blackout period, long expiration period,
      // and short update period.
      const std::string policy_yaml = R"EOF(
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.client_side_weighted_round_robin
              typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin
                  blackout_period:
                      seconds: 1
                  weight_expiration_period:
                      seconds: 180
                  weight_update_period:
                      seconds: 1
          )EOF";

      TestUtility::loadFromYaml(policy_yaml, *policy);
    });

    HttpIntegrationTest::initialize();
  }

  Http::TestResponseHeaderMapImpl
  responseHeadersWithLoadReport(int backend_index, double application_utilization, double qps) {
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_application_utilization(application_utilization);
    orca_load_report.mutable_named_metrics()->insert({"backend_index", backend_index});
    orca_load_report.set_rps_fractional(qps);
    std::string proto_string = TestUtility::getProtobufBinaryStringFromMessage(orca_load_report);
    std::string orca_load_report_header_bin =
        Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_headers.addCopy("endpoint-load-metrics-bin", orca_load_report_header_bin);
    return response_headers;
  }

  void sendRequestsAndTrackUpstreamUsage(uint64_t number_of_requests,
                                         std::vector<uint64_t>& upstream_usage) {
    // Expected number of upstreams.
    upstream_usage.resize(3);
    ENVOY_LOG(trace, "Start sending {} requests.", number_of_requests);

    for (uint64_t i = 0; i < number_of_requests; i++) {
      ENVOY_LOG(trace, "Before request {}.", i);

      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());
      upstream_usage[upstream_index.value()]++;

      // All hosts report the same utilization, but different QPS, so their
      // weights will be different.
      upstream_request_->encodeHeaders(
          responseHeadersWithLoadReport(upstream_index.value(), 0.5,
                                        1 * (upstream_index.value() + 1)),
          true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
      ENVOY_LOG(trace, "After request {}.", i);
    }
  }

  void runNormalLoadBalancing() {
    std::vector<uint64_t> indexs;

    // Initial requests use round robin because client-side reported weights
    // are ignored during 1s blackout period.
    std::vector<uint64_t> initial_usage;
    sendRequestsAndTrackUpstreamUsage(50, initial_usage);

    ENVOY_LOG(trace, "initial_usage {}", initial_usage);

    // Wait longer than blackout period to ensure that client side weights are
    // applied.
    timeSystem().advanceTimeWait(std::chrono::seconds(2));

    // Send more requests expecting weights to be applied, so upstream hosts are
    // used proportionally to their weights.
    std::vector<uint64_t> weighted_usage;
    sendRequestsAndTrackUpstreamUsage(100, weighted_usage);
    ENVOY_LOG(trace, "weighted_usage {}", weighted_usage);
    EXPECT_LT(weighted_usage[0], weighted_usage[1]);
    EXPECT_LT(weighted_usage[1], weighted_usage[2]);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientSideWeightedRoundRobinIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientSideWeightedRoundRobinIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

} // namespace
} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
