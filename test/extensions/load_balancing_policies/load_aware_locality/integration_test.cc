#include <chrono>
#include <cstdint>
#include <numeric>
#include <vector>

#include "source/common/common/base64.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

class LoadAwareLocalityIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  LoadAwareLocalityIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamCount(num_upstreams_);
    use_bootstrap_node_metadata_ = true;
  }

  void initializeConfig(double variance_threshold = 0.1, double probe_percentage = 0.1,
                        int weight_update_period_seconds = 10, double ewma_alpha = 1.0,
                        std::vector<std::string> remote_zones = {"zone-b"}) {
    num_upstreams_ = static_cast<uint32_t>(2 * (1 + remote_zones.size()));
    setUpstreamCount(num_upstreams_);

    const auto ip_version = GetParam();
    config_helper_.addConfigModifier([ip_version, variance_threshold, probe_percentage,
                                      weight_update_period_seconds, ewma_alpha, remote_zones](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* node = bootstrap.mutable_node();
      node->set_id("node_name");
      node->set_cluster("cluster_name");
      auto* locality = node->mutable_locality();
      locality->set_region("test-region");
      locality->set_zone("zone-a");

      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster->name() == "cluster_0");
      cluster->mutable_load_assignment()->clear_endpoints();
      cluster->mutable_load_assignment()->set_cluster_name("cluster_0");

      const std::string local_address = Network::Test::getLoopbackAddressString(ip_version);
      std::vector<std::string> all_zones = {"zone-a"};
      all_zones.insert(all_zones.end(), remote_zones.begin(), remote_zones.end());
      for (const auto& zone : all_zones) {
        auto* locality_pb = cluster->mutable_load_assignment()->add_endpoints();
        locality_pb->mutable_locality()->set_region("test-region");
        locality_pb->mutable_locality()->set_zone(zone);
        for (int i = 0; i < 2; ++i) {
          auto* addr = locality_pb->add_lb_endpoints()
                           ->mutable_endpoint()
                           ->mutable_address()
                           ->mutable_socket_address();
          addr->set_address(local_address);
          addr->set_port_value(0);
        }
      }

      const std::string policy_yaml = fmt::format(R"EOF(
              policies:
              - typed_extension_config:
                  name: envoy.load_balancing_policies.load_aware_locality
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality
                    endpoint_picking_policy:
                      policies:
                      - typed_extension_config:
                          name: envoy.load_balancing_policies.round_robin
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
                    weight_update_period:
                      seconds: {}
                    utilization_variance_threshold:
                      value: {}
                    probe_percentage:
                      value: {}
                    ewma_alpha:
                      value: {}
              )EOF",
                                                  weight_update_period_seconds, variance_threshold,
                                                  probe_percentage, ewma_alpha);
      TestUtility::loadFromYaml(policy_yaml, *cluster->mutable_load_balancing_policy());
    });

    HttpIntegrationTest::initialize();
  }

  Http::TestResponseHeaderMapImpl
  responseHeadersWithOrcaUtilization(double application_utilization) {
    xds::data::orca::v3::OrcaLoadReport report;
    report.set_application_utilization(application_utilization);
    const std::string proto_string = TestUtility::getProtobufBinaryStringFromMessage(report);
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_headers.addCopy("endpoint-load-metrics-bin",
                             Envoy::Base64::encode(proto_string.c_str(), proto_string.length()));
    return response_headers;
  }

  uint64_t sendRequestWithOrcaResponse(const std::vector<double>& utilizations) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    std::vector<uint64_t> upstream_indices(num_upstreams_);
    std::iota(upstream_indices.begin(), upstream_indices.end(), 0);
    auto upstream_index = waitForNextUpstreamRequest(upstream_indices);
    RELEASE_ASSERT(upstream_index.has_value(), "Expected upstream request");

    upstream_request_->encodeHeaders(
        responseHeadersWithOrcaUtilization(utilizations[*upstream_index]), true);
    RELEASE_ASSERT(response->waitForEndStream(), "Expected response");
    cleanupUpstreamAndDownstream();
    return *upstream_index;
  }

  std::vector<uint64_t> sendRequestsAndTrack(uint64_t count,
                                             const std::vector<double>& utilizations) {
    std::vector<uint64_t> usage(num_upstreams_, 0);
    for (uint64_t i = 0; i < count; ++i) {
      usage[sendRequestWithOrcaResponse(utilizations)]++;
    }
    return usage;
  }

  void advanceWeightTick(uint64_t expected_counter) {
    timeSystem().advanceTimeWait(std::chrono::seconds(11));
    test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures",
                                   expected_counter);
  }

  void seedWithTwoCycles(const std::vector<double>& utilizations, uint64_t starting_count = 1) {
    sendRequestsAndTrack(30, utilizations);
    advanceWeightTick(starting_count + 1);
    sendRequestsAndTrack(30, utilizations);
    advanceWeightTick(starting_count + 2);
  }

  uint64_t zoneTraffic(const std::vector<uint64_t>& usage, size_t zone_index) const {
    const size_t start = zone_index * 2;
    return usage[start] + usage[start + 1];
  }

  uint64_t localTraffic(const std::vector<uint64_t>& usage) const { return zoneTraffic(usage, 0); }

  uint64_t remoteTraffic(const std::vector<uint64_t>& usage) const {
    uint64_t total = 0;
    for (size_t zone = 1; zone < num_upstreams_ / 2; ++zone) {
      total += zoneTraffic(usage, zone);
    }
    return total;
  }

protected:
  uint32_t num_upstreams_{4};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LoadAwareLocalityIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(LoadAwareLocalityIntegrationTest, StrictLocalWithRoundRobinWithinLocality) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.0,
                   /*weight_update_period_seconds=*/10);

  const std::vector<double> utilizations = {0.05, 0.05, 0.05, 0.05};
  sendRequestsAndTrack(30, utilizations);
  advanceWeightTick(2);

  const auto usage = sendRequestsAndTrack(100, utilizations);

  EXPECT_EQ(100u, localTraffic(usage));
  EXPECT_EQ(0u, remoteTraffic(usage));
  EXPECT_GE(usage[0], 30u);
  EXPECT_LE(usage[0], 70u);
  EXPECT_GE(usage[1], 30u);
  EXPECT_LE(usage[1], 70u);
}

TEST_P(LoadAwareLocalityIntegrationTest, AdaptiveSpillAndRecovery) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  const std::vector<double> overloaded_local = {0.9, 0.9, 0.2, 0.2};
  seedWithTwoCycles(overloaded_local);

  const auto phase1_usage = sendRequestsAndTrack(100, overloaded_local);
  const uint64_t phase1_local = localTraffic(phase1_usage);
  const uint64_t phase1_remote = remoteTraffic(phase1_usage);
  EXPECT_GE(phase1_remote, 60u);
  EXPECT_GT(phase1_remote, phase1_local);

  const std::vector<double> rebalanced = {0.3, 0.3, 0.3, 0.3};
  sendRequestsAndTrack(60, rebalanced);
  advanceWeightTick(4);

  const auto phase2_usage = sendRequestsAndTrack(100, rebalanced);
  const uint64_t phase2_local = localTraffic(phase2_usage);
  EXPECT_GT(phase2_local, phase1_local);
  EXPECT_GE(phase2_local, 35u);
}

TEST_P(LoadAwareLocalityIntegrationTest, EwmaDampensSpike) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10, /*ewma_alpha=*/0.5);

  const std::vector<double> baseline = {0.2, 0.2, 0.2, 0.2};
  seedWithTwoCycles(baseline);

  const std::vector<double> spiked_local = {0.9, 0.9, 0.2, 0.2};
  sendRequestsAndTrack(30, spiked_local);
  advanceWeightTick(4);

  const auto usage = sendRequestsAndTrack(100, spiked_local);
  const uint64_t remote = remoteTraffic(usage);
  EXPECT_GE(remote, 40u);
  EXPECT_LE(remote, 85u);
}

TEST_P(LoadAwareLocalityIntegrationTest, ThreeLocalityDistribution) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10, /*ewma_alpha=*/1.0,
                   /*remote_zones=*/{"zone-b", "zone-c"});

  const std::vector<double> utilizations = {0.8, 0.8, 0.3, 0.3, 0.5, 0.5};
  seedWithTwoCycles(utilizations);

  const auto usage = sendRequestsAndTrack(400, utilizations);
  const uint64_t zone_a = zoneTraffic(usage, 0);
  const uint64_t zone_b = zoneTraffic(usage, 1);
  const uint64_t zone_c = zoneTraffic(usage, 2);

  EXPECT_GT(zone_a, 0u);
  EXPECT_GT(zone_b, 0u);
  EXPECT_GT(zone_c, 0u);
  EXPECT_GT(zone_b, zone_c);
  EXPECT_GT(zone_c, zone_a);
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
