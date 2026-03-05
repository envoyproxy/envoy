#include <chrono>
#include <cstdint>
#include <numeric>

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
    // Default: 4 upstreams — 2 in zone-a (local), 2 in zone-b (remote).
    setUpstreamCount(num_upstreams_);
    // Use the bootstrap node metadata (including locality) instead of the
    // hardcoded "zone_name" from the test server options. This is required for
    // hasLocalLocality() to return true when the bootstrap node locality matches
    // an endpoint locality.
    use_bootstrap_node_metadata_ = true;
  }

  // Initialize with a local zone-a and the given remote zones (default: {"zone-b"}).
  // Each zone has 2 endpoints; num_upstreams_ is updated accordingly.
  // Upstream indices are contiguous: zone 0 → [0,1], zone 1 → [2,3], zone 2 → [4,5], etc.
  void initializeConfig(double variance_threshold = 0.1, double probe_percentage = 0.1,
                        int weight_update_period_seconds = 10, double ewma_alpha = 1.0,
                        std::vector<std::string> remote_zones = {"zone-b"}) {
    num_upstreams_ = static_cast<uint32_t>(2 * (1 + remote_zones.size()));
    setUpstreamCount(num_upstreams_);

    const auto ip_version = GetParam();
    config_helper_.addConfigModifier([ip_version, variance_threshold, probe_percentage,
                                      weight_update_period_seconds, ewma_alpha, remote_zones](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Set node metadata so the node locality matches zone-a (the local zone).
      auto* node = bootstrap.mutable_node();
      node->set_id("node_name");
      node->set_cluster("cluster_name");
      auto* locality = node->mutable_locality();
      locality->set_region("test-region");
      locality->set_zone("zone-a");

      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      cluster_0->mutable_load_assignment()->clear_endpoints();
      cluster_0->mutable_load_assignment()->set_cluster_name("cluster_0");

      const std::string local_address = Network::Test::getLoopbackAddressString(ip_version);

      // Build one locality per zone: zone-a first (local), then each remote zone.
      // Each locality gets 2 endpoints.
      std::vector<std::string> all_zones = {"zone-a"};
      all_zones.insert(all_zones.end(), remote_zones.begin(), remote_zones.end());
      for (const auto& zone : all_zones) {
        auto* locality_pb = cluster_0->mutable_load_assignment()->add_endpoints();
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

      // Configure load_aware_locality LB policy with round_robin child.
      auto* policy = cluster_0->mutable_load_balancing_policy();
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
      TestUtility::loadFromYaml(policy_yaml, *policy);
    });

    HttpIntegrationTest::initialize();
  }

  Http::TestResponseHeaderMapImpl
  responseHeadersWithOrcaUtilization(double application_utilization) {
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_application_utilization(application_utilization);
    std::string proto_string = TestUtility::getProtobufBinaryStringFromMessage(orca_load_report);
    std::string orca_load_report_header_bin =
        Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_headers.addCopy("endpoint-load-metrics-bin", orca_load_report_header_bin);
    return response_headers;
  }

  // Send a single request, respond with ORCA utilization, return the upstream index.
  uint64_t sendRequestWithOrcaResponse(const std::vector<double>& utilizations) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
    auto response = codec_client_->makeRequestWithBody(request_headers, 0);
    std::vector<uint64_t> upstream_indices(num_upstreams_);
    std::iota(upstream_indices.begin(), upstream_indices.end(), 0);
    auto upstream_index = waitForNextUpstreamRequest(upstream_indices);
    RELEASE_ASSERT(upstream_index.has_value(), "Expected upstream request");

    double util = utilizations[upstream_index.value()];
    upstream_request_->encodeHeaders(responseHeadersWithOrcaUtilization(util), true);

    RELEASE_ASSERT(response->waitForEndStream(), "Expected response");
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
    return upstream_index.value();
  }

  // Send multiple requests and return per-upstream usage counts.
  std::vector<uint64_t> sendRequestsAndTrack(uint64_t count,
                                             const std::vector<double>& utilizations) {
    std::vector<uint64_t> upstream_usage(num_upstreams_, 0);
    for (uint64_t i = 0; i < count; ++i) {
      auto idx = sendRequestWithOrcaResponse(utilizations);
      upstream_usage[idx]++;
    }
    return upstream_usage;
  }

  // Send two 30-request seeding rounds with timer advances to ensure all endpoints have
  // ORCA data before the measurement phase. With only probe=0.1, the initial all_local
  // cycle may skip some remote hosts (P ≈ 22% per zone with 30 requests), so a second
  // cycle is needed to guarantee all hosts accumulate data.
  // starting_count: lb_recalculate_zone_structures value before this call (1 after initialize()).
  // After returning, the counter is at starting_count + 2.
  void seedWithTwoCycles(const std::vector<double>& utilizations, uint64_t starting_count = 1) {
    sendRequestsAndTrack(30, utilizations);
    timeSystem().advanceTimeWait(std::chrono::seconds(11));
    test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures",
                                   starting_count + 1);

    sendRequestsAndTrack(30, utilizations);
    timeSystem().advanceTimeWait(std::chrono::seconds(11));
    test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures",
                                   starting_count + 2);
  }

protected:
  uint32_t num_upstreams_{4};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LoadAwareLocalityIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// With equal utilization across localities, traffic stays local due to variance threshold.
// Uses a long weight_update_period to ensure the timer doesn't fire during seeding with
// partial ORCA data, which would cause the variance check to fail spuriously.
TEST_P(LoadAwareLocalityIntegrationTest, LocalLocalityPreference) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  std::vector<double> utilizations = {0.3, 0.3, 0.3, 0.3};
  seedWithTwoCycles(utilizations);

  // After recomputing: local_util=0.3, remote_avg=0.3.
  // 0.3 <= 0.3 + 0.1 → all_local. Probe gives 10% to remote.
  auto usage = sendRequestsAndTrack(100, utilizations);

  uint64_t local_count = usage[0] + usage[1];
  uint64_t remote_count = usage[2] + usage[3];

  // Expect majority local. Probe percentage sends ~10% remote.
  EXPECT_GE(local_count, 75u);
  EXPECT_LE(remote_count, 25u);
}

// When local zone is overloaded, traffic spills proportionally to remote.
TEST_P(LoadAwareLocalityIntegrationTest, SpillToRemoteOnOverload) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  std::vector<double> utilizations = {0.9, 0.9, 0.2, 0.2};
  seedWithTwoCycles(utilizations);

  // After recomputing: local_util=0.9, remote_avg=0.2.
  // 0.9 > 0.2 + 0.1 → NOT all_local.
  // Weights: local = 2 * (1-0.9) = 0.2, remote = 2 * (1-0.2) = 1.6.
  // Remote should get ~89% of traffic.
  auto usage = sendRequestsAndTrack(100, utilizations);

  uint64_t local_count = usage[0] + usage[1];
  uint64_t remote_count = usage[2] + usage[3];

  EXPECT_GT(remote_count, local_count);
  EXPECT_GE(remote_count, 70u);
}

// When all localities are at 100% utilization, every locality's headroom is 0.
// The fallback distributes traffic proportional to healthy host count (50/50 since
// both localities have 2 hosts each), bypassing the probe step entirely.
TEST_P(LoadAwareLocalityIntegrationTest, AllLocalitiesOverloaded) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  std::vector<double> utilizations = {1.0, 1.0, 1.0, 1.0};
  seedWithTwoCycles(utilizations);

  // After recomputing: every locality has headroom = 0, total_weight = 0.
  // Fallback: weights proportional to host_count → [2, 2], total = 4 → 50/50 split.
  // The probe step is skipped because pre-fallback total is 0.
  auto usage = sendRequestsAndTrack(100, utilizations);

  uint64_t local_count = usage[0] + usage[1];
  uint64_t remote_count = usage[2] + usage[3];

  // Both localities should receive significant traffic (roughly 50% each).
  // Using a conservative 25% floor to tolerate randomness over 100 samples.
  EXPECT_GE(local_count, 25u) << "local=" << local_count << " remote=" << remote_count;
  EXPECT_GE(remote_count, 25u) << "local=" << local_count << " remote=" << remote_count;
}

// In all_local mode, the child round_robin policy distributes evenly across the two
// local endpoints. Uses probe_percentage=0 and low local utilization so that all
// requests stay in locality 0. Also verifies that with probe=0 no traffic leaks to remote.
TEST_P(LoadAwareLocalityIntegrationTest, RoundRobinWithinLocality) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.0,
                   /*weight_update_period_seconds=*/10);

  // Local hosts report very low utilization (0.05). Remote hosts receive no requests
  // (probe=0), so their utilization defaults to 0.0.
  // Variance check: local (0.05) <= remote (0.0) + threshold (0.1) → all_local.
  std::vector<double> utilizations = {0.05, 0.05, 0.05, 0.05};
  sendRequestsAndTrack(30, utilizations);
  timeSystem().advanceTimeWait(std::chrono::seconds(11));
  test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures", 2);

  // With probe_percentage=0, all_local → weights=[1,0] → strictly 100% local.
  // Child RR distributes between host 0 and host 1.
  auto usage = sendRequestsAndTrack(100, utilizations);

  EXPECT_EQ(usage[2] + usage[3], 0u)
      << "Expected zero remote traffic with probe_percentage=0 in all_local mode";
  EXPECT_EQ(usage[0] + usage[1], 100u);

  // RR within the locality gives approximately equal split (allow ±20% margin).
  EXPECT_GE(usage[0], 30u) << "host 0 should receive ~50% of local traffic";
  EXPECT_LE(usage[0], 70u) << "host 0 should receive ~50% of local traffic";
  EXPECT_GE(usage[1], 30u) << "host 1 should receive ~50% of local traffic";
  EXPECT_LE(usage[1], 70u) << "host 1 should receive ~50% of local traffic";
}

// With probe_percentage=0.3, even in all_local mode, ~30% of traffic is sent to
// remote localities for continuous load monitoring.
TEST_P(LoadAwareLocalityIntegrationTest, HighProbePercentage) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.3,
                   /*weight_update_period_seconds=*/10);

  std::vector<double> utilizations = {0.3, 0.3, 0.3, 0.3};
  seedWithTwoCycles(utilizations);

  // After recomputing: local_util=0.3, remote_avg=0.3 → all_local=true.
  // Probe redistributes 30% to remote: weights before probe [1, 0], total=1.
  // probe_target = 1 * 0.3 = 0.3, deficit = 0.3.
  // After probe: weights = [0.7, 0.3] → remote gets ~30%.
  auto usage = sendRequestsAndTrack(100, utilizations);

  uint64_t remote_count = usage[2] + usage[3];

  // Expect ~30% remote. 3-sigma range for 100 trials at p=0.3: [30 ± 14] → [16%, 44%].
  EXPECT_GE(remote_count, 14u) << "Expected ~30% remote with probe_percentage=0.3";
  EXPECT_LE(remote_count, 50u) << "Expected ~30% remote with probe_percentage=0.3";
}

// Verifies the `<=` (not `<`) boundary in the variance threshold check.
// When local_util exactly equals remote_util + variance_threshold, all_local triggers.
TEST_P(LoadAwareLocalityIntegrationTest, LocalAtExactThreshold) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  // local=0.3, remote=0.2. With threshold=0.1: local (0.3) <= remote (0.2) + 0.1 = 0.3
  // → exactly at boundary → all_local (the condition uses <=, not <).
  // Using probe=0.1 so remote hosts receive seeding traffic and report util=0.2.
  std::vector<double> utilizations = {0.3, 0.3, 0.2, 0.2};
  seedWithTwoCycles(utilizations);

  // all_local=true, probe=0.1 → ~90% local, ~10% remote.
  auto usage = sendRequestsAndTrack(100, utilizations);

  uint64_t local_count = usage[0] + usage[1];

  // Local should dominate. 90-request expectation with ±15% margin.
  EXPECT_GE(local_count, 70u) << "Expected all_local at exact threshold: local=" << local_count
                              << " u0=" << usage[0] << " u1=" << usage[1] << " u2=" << usage[2]
                              << " u3=" << usage[3];
}

// With ewma_alpha=0.5, a sudden spike in local utilization is dampened: the smoothed
// value only moves halfway toward the new observation each tick, so spill is partial
// after the first timer cycle rather than jumping to the fully-converged value.
TEST_P(LoadAwareLocalityIntegrationTest, EwmaAlphaDampensSpike) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10, /*ewma_alpha=*/0.5);

  // Phase 1: Seed baseline with low equal utilization.
  std::vector<double> low_util = {0.2, 0.2, 0.2, 0.2};
  seedWithTwoCycles(low_util);
  // Tick 2 (cycle 1): cold-start — smoothed-utilization set directly from raw avg_utils_,
  // no EWMA blend on the first tick with data. Tick 3 (cycle 2): first actual EWMA blend,
  // smoothed_i = 0.5*0.2 + 0.5*0.2 = 0.2. Nominal result: smoothed={0.2,0.2}, all_local=true.

  // Phase 2: Spike local utilization to 0.9. Since we're still in all_local mode,
  // ~90% of requests go local (reporting 0.9). Remote (~10%) still report 0.2.
  std::vector<double> high_local_util = {0.9, 0.9, 0.2, 0.2};
  sendRequestsAndTrack(30, high_local_util);
  timeSystem().advanceTimeWait(std::chrono::seconds(11));
  test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures", 4);
  // Tick 4 EWMA blend (alpha=0.5, baseline ≈ {0.2, 0.2} from seeding):
  //   smoothed_local = 0.5 * 0.9 + 0.5 * 0.2 = 0.55  (damped, not full 0.9)
  //   smoothed_remote = 0.5 * 0.2 + 0.5 * 0.2 = 0.2
  // Variance: 0.55 > 0.2 + 0.1 = 0.3 → NOT all_local → spill begins.
  // Weights: local = 2*(1-0.55) = 0.9, remote = 2*(1-0.2) = 1.6, total = 2.5
  // remote% ≈ 64%. Contrast: alpha=1.0 would give smoothed_local=0.9, remote%≈89%.

  auto usage = sendRequestsAndTrack(100, high_local_util);
  uint64_t remote_count = usage[2] + usage[3];

  // With alpha=0.5, spill is ~64%. Verify it's clearly above 0 (dampened spike
  // still triggers spill) but substantially below the alpha=1.0 steady state (~89%).
  // 3-sigma bounds for p=0.64 over 100 trials: [64 ± 14] → [50, 78].
  EXPECT_GE(remote_count, 40u)
      << "Expected partial spill (~64%) after one EWMA tick with alpha=0.5; remote="
      << remote_count;
  EXPECT_LE(remote_count, 85u)
      << "Expected less than full spill (~89%) due to EWMA dampening; remote=" << remote_count;
}

// Verifies the policy dynamically adapts when utilization patterns change across timer
// cycles. This is the core value proposition: the closed-loop timer-driven weight
// recomputing reacts to shifting load.
TEST_P(LoadAwareLocalityIntegrationTest, LoadShiftOnUtilizationChange) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10);

  // Phase 1: Local overloaded, remote low → traffic should spill to remote.
  std::vector<double> phase1_util = {0.9, 0.9, 0.2, 0.2};
  seedWithTwoCycles(phase1_util);

  // local_util=0.9, remote_avg=0.2. 0.9 > 0.2 + 0.1 → NOT all_local.
  // Weights: local=2*(1-0.9)=0.2, remote=2*(1-0.2)=1.6 → remote ~89%.
  auto usage_p1 = sendRequestsAndTrack(100, phase1_util);
  uint64_t remote_p1 = usage_p1[2] + usage_p1[3];
  EXPECT_GE(remote_p1, 60u) << "Phase 1: expected majority remote when local is overloaded; remote="
                            << remote_p1;

  // Phase 2: All utilization equalizes → traffic should shift back toward local.
  // Use more seeding requests because phase 1 weights are still active (remote-heavy),
  // so local hosts need enough requests to accumulate fresh ORCA data.
  std::vector<double> phase2_util = {0.3, 0.3, 0.3, 0.3};
  sendRequestsAndTrack(60, phase2_util);
  timeSystem().advanceTimeWait(std::chrono::seconds(11));
  test_server_->waitForCounterGe("cluster.cluster_0.lb_recalculate_zone_structures", 4);

  // local_util=0.3, remote_avg=0.3. 0.3 <= 0.3 + 0.1 → all_local.
  // The system may not fully converge in a single cycle after the phase 1 remote-heavy
  // distribution, so we validate the adaptation direction rather than full convergence.
  auto usage_p2 = sendRequestsAndTrack(100, phase2_util);
  uint64_t local_p2 = usage_p2[0] + usage_p2[1];
  uint64_t local_p1 = usage_p1[0] + usage_p1[1];
  // Phase 2 local traffic must be significantly higher than phase 1 local traffic,
  // proving the policy adapted to the new utilization data.
  EXPECT_GT(local_p2, local_p1)
      << "Phase 2 should route more locally than phase 1 after utilization equalizes;"
      << " local_p2=" << local_p2 << " local_p1=" << local_p1;
  EXPECT_GE(local_p2, 35u)
      << "Phase 2: expected meaningful local shift after utilization equalizes; local=" << local_p2;
}

// Verifies correct weight distribution across 3 localities (all other tests use only 2).
// Exercises the 3-locality config loading path, multi-remote probe distribution, and
// 3-way weighted random selection.
TEST_P(LoadAwareLocalityIntegrationTest, ThreeLocalitiesDistribution) {
  initializeConfig(/*variance_threshold=*/0.1, /*probe_percentage=*/0.1,
                   /*weight_update_period_seconds=*/10, /*ewma_alpha=*/1.0,
                   /*remote_zones=*/{"zone-b", "zone-c"});

  // zone-a (local, hosts 0-1): util=0.8
  // zone-b (remote, hosts 2-3): util=0.3
  // zone-c (remote, hosts 4-5): util=0.5
  // remote_avg = (0.3 * 2 + 0.5 * 2) / 4 = 0.4
  // Variance check: 0.8 > 0.4 + 0.1 → NOT all_local.
  // Weights: zone-a = 2*(1-0.8) = 0.4, zone-b = 2*(1-0.3) = 1.4, zone-c = 2*(1-0.5) = 1.0
  // Total = 2.8 → zone-a ~14%, zone-b ~50%, zone-c ~36%.
  std::vector<double> utilizations = {0.8, 0.8, 0.3, 0.3, 0.5, 0.5};
  seedWithTwoCycles(utilizations);

  // Use 400 samples so the expected ordering is statistically robust.
  // With 400 samples, P(zone-b count < zone-c count) ≈ 0.1% (normal approx:
  //   E[B-C]=56, Var[B-C]=336, σ=18.3, z=-3.06).
  auto usage = sendRequestsAndTrack(400, utilizations);
  uint64_t zone_a = usage[0] + usage[1];
  uint64_t zone_b = usage[2] + usage[3];
  uint64_t zone_c = usage[4] + usage[5];

  EXPECT_GT(zone_a, 0u) << "zone-a should receive some traffic";
  EXPECT_GT(zone_b, 0u) << "zone-b should receive some traffic";
  EXPECT_GT(zone_c, 0u) << "zone-c should receive some traffic";

  // Ordering: zone-b (most headroom) > zone-c > zone-a (least headroom).
  EXPECT_GT(zone_b, zone_c)
      << "zone-b (util=0.3, weight=1.4) should get more traffic than zone-c (util=0.5, weight=1.0)"
      << " zone_b=" << zone_b << " zone_c=" << zone_c;
  EXPECT_GT(zone_c, zone_a)
      << "zone-c (util=0.5, weight=1.0) should get more traffic than zone-a (util=0.8, weight=0.4)"
      << " zone_c=" << zone_c << " zone_a=" << zone_a;
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
