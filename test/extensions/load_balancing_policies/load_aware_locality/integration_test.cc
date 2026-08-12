#include <chrono>
#include <cstdint>
#include <numeric>
#include <vector>

#include "source/common/common/base64.h"
#include "source/common/grpc/common.h"
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
    use_bootstrap_node_metadata_ = true;
  }

  void TearDown() override { HttpIntegrationTest::cleanupUpstreamAndDownstream(); }

  void initializeConfig(double variance_threshold = 0.1, double remote_probe_fraction = 0.1,
                        int weight_update_period_seconds = 10,
                        int smoothing_time_constant_seconds = 1,
                        std::vector<std::string> remote_zones = {"zone-b"}) {
    num_upstreams_ = static_cast<uint32_t>(2 * (1 + remote_zones.size()));
    setUpstreamCount(num_upstreams_);

    const auto ip_version = GetParam();
    config_helper_.addConfigModifier(
        [ip_version, variance_threshold, remote_probe_fraction, weight_update_period_seconds,
         smoothing_time_constant_seconds,
         remote_zones](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

          const std::string policy_yaml =
              fmt::format(R"EOF(
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
                    remote_probe_fraction:
                      value: {}
                    smoothing_time_constant:
                      seconds: {}
              )EOF",
                          weight_update_period_seconds, variance_threshold, remote_probe_fraction,
                          smoothing_time_constant_seconds);
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

  void advanceWeightTick(const std::string& counter_name = "") {
    // Capture before advancing: initialize() already ran a no-data tick, so an absolute
    // threshold would be satisfied one tick early and provide no ordering.
    uint64_t before = 0;
    std::string counter;
    if (!counter_name.empty()) {
      counter = fmt::format("cluster.cluster_0.load_aware_locality.{}", counter_name);
      const auto c = test_server_->counter(counter);
      before = c != nullptr ? c->value() : 0;
    }
    timeSystem().advanceTimeWait(std::chrono::seconds(11));
    if (!counter.empty()) {
      test_server_->waitForCounter(counter, testing::Ge(before + 1));
    }
  }

  // Sends requests until every upstream has served one, so every host has reported utilization
  // and the next tick's weight-computation branch is deterministic instead of sampling-dependent.
  void seedAllUpstreams(const std::vector<double>& utilizations) {
    std::vector<bool> seen(num_upstreams_, false);
    uint32_t covered = 0;
    for (int i = 0; i < 500 && covered < num_upstreams_; ++i) {
      const uint64_t idx = sendRequestWithOrcaResponse(utilizations);
      if (!seen[idx]) {
        seen[idx] = true;
        ++covered;
      }
    }
    ASSERT_EQ(num_upstreams_, covered);
  }

  void seedWithTwoCycles(const std::vector<double>& utilizations,
                         const std::string& counter_name = "") {
    seedAllUpstreams(utilizations);
    advanceWeightTick(counter_name);
    sendRequestsAndTrack(30, utilizations);
    advanceWeightTick(counter_name);
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
  initializeConfig(/*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.0,
                   /*weight_update_period_seconds=*/10);

  const std::vector<double> utilizations = {0.05, 0.05, 0.05, 0.05};
  sendRequestsAndTrack(30, utilizations);
  advanceWeightTick("local_preferred_total");

  const auto usage = sendRequestsAndTrack(100, utilizations);

  EXPECT_EQ(100u, localTraffic(usage));
  EXPECT_EQ(0u, remoteTraffic(usage));
  EXPECT_GE(usage[0], 30u);
  EXPECT_LE(usage[0], 70u);
  EXPECT_GE(usage[1], 30u);
  EXPECT_LE(usage[1], 70u);
}

TEST_P(LoadAwareLocalityIntegrationTest, ProbeFloorKeepsRemoteSampledInAllLocalMode) {
  // Balanced utilization keeps the variance snap in all-local mode; the probe floor must still
  // route ~10% of traffic to the remote locality end-to-end so its ORCA data stays fresh.
  initializeConfig(/*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.1,
                   /*weight_update_period_seconds=*/10);

  const std::vector<double> balanced = {0.4, 0.4, 0.4, 0.4};
  seedWithTwoCycles(balanced, "probe_active_total");

  const auto usage = sendRequestsAndTrack(400, balanced);
  const uint64_t local = localTraffic(usage);
  const uint64_t remote = remoteTraffic(usage);
  // Expected remote share is the probe floor: 10% of 400 = ~40. Generous sampling slack, but
  // the floor must be visibly active (not zero, not dominating).
  EXPECT_GE(remote, 15u);
  EXPECT_LE(remote, 90u);
  EXPECT_GT(local, remote);
}

TEST_P(LoadAwareLocalityIntegrationTest, AdaptiveSpillAndRecovery) {
  initializeConfig(/*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.1,
                   /*weight_update_period_seconds=*/10);

  const std::vector<double> overloaded_local = {0.9, 0.9, 0.2, 0.2};
  seedWithTwoCycles(overloaded_local, "spill_active_total");

  const auto phase1_usage = sendRequestsAndTrack(100, overloaded_local);
  const uint64_t phase1_local = localTraffic(phase1_usage);
  const uint64_t phase1_remote = remoteTraffic(phase1_usage);
  EXPECT_GE(phase1_remote, 60u);
  EXPECT_GT(phase1_remote, phase1_local);

  const std::vector<double> rebalanced = {0.3, 0.3, 0.3, 0.3};
  sendRequestsAndTrack(60, rebalanced);
  // Rebalanced utilization snaps local, incrementing local_preferred_total.
  advanceWeightTick("local_preferred_total");

  const auto phase2_usage = sendRequestsAndTrack(100, rebalanced);
  const uint64_t phase2_local = localTraffic(phase2_usage);
  EXPECT_GT(phase2_local, phase1_local);
  EXPECT_GE(phase2_local, 35u);
}

TEST_P(LoadAwareLocalityIntegrationTest, EwmaDampensSpike) {
  // smoothing_time_constant=14s with weight_update_period=10s yields alpha ~= 0.51.
  initializeConfig(/*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.1,
                   /*weight_update_period_seconds=*/10, /*smoothing_time_constant_seconds=*/14);

  const std::vector<double> baseline = {0.2, 0.2, 0.2, 0.2};
  seedWithTwoCycles(baseline, "local_preferred_total");

  const std::vector<double> spiked_local = {0.9, 0.9, 0.2, 0.2};
  sendRequestsAndTrack(30, spiked_local);
  advanceWeightTick("spill_active_total");

  const auto usage = sendRequestsAndTrack(100, spiked_local);
  const uint64_t remote = remoteTraffic(usage);
  EXPECT_GE(remote, 40u);
  EXPECT_LE(remote, 85u);
}

TEST_P(LoadAwareLocalityIntegrationTest, ThreeLocalityDistribution) {
  initializeConfig(/*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.1,
                   /*weight_update_period_seconds=*/10, /*smoothing_time_constant_seconds=*/1,
                   /*remote_zones=*/{"zone-b", "zone-c"});

  const std::vector<double> utilizations = {0.8, 0.8, 0.3, 0.3, 0.5, 0.5};
  seedWithTwoCycles(utilizations, "spill_active_total");

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

// Full load_balancing_policy configs for the OOB fixture. Both use a 1s weight
// tick and 1s OOB reporting period so tests converge quickly.
constexpr absl::string_view kOobWithRoundRobinChildYaml = R"EOF(
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
        seconds: 1
      smoothing_time_constant:
        seconds: 1
      enable_oob_load_report: true
      oob_reporting_period:
        seconds: 1
)EOF";

// LoadAwareLocality enables OOB; the ClientSideWeightedRoundRobin child does not (enable_oob_load_report omitted,
// defaults false). Used to prove fan-out: reports arriving on
// load_aware_locality's own stream still reach the ClientSideWeightedRoundRobin child's host data.
constexpr absl::string_view kOobLalOnlyCswrrChildYaml = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.load_aware_locality
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality
      endpoint_picking_policy:
        policies:
        - typed_extension_config:
            name: envoy.load_balancing_policies.client_side_weighted_round_robin
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin
      weight_update_period:
        seconds: 1
      smoothing_time_constant:
        seconds: 1
      enable_oob_load_report: true
      oob_reporting_period:
        seconds: 1
)EOF";

// Both the locality policy AND the ClientSideWeightedRoundRobin child enable OOB.
constexpr absl::string_view kOobBothLevelsYaml = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.load_aware_locality
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality
      endpoint_picking_policy:
        policies:
        - typed_extension_config:
            name: envoy.load_balancing_policies.client_side_weighted_round_robin
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin
              enable_oob_load_report: true
              oob_reporting_period:
                seconds: 1
      weight_update_period:
        seconds: 1
      smoothing_time_constant:
        seconds: 1
      enable_oob_load_report: true
      oob_reporting_period:
        seconds: 1
)EOF";

// OOB integration fixture: HTTP/2 upstreams (OOB streams are gRPC), two hosts
// per zone, Envoy local zone is zone-a. Modeled on the ClientSideWeightedRoundRobin OOB fixture.
class LoadAwareLocalityOobIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  LoadAwareLocalityOobIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    use_bootstrap_node_metadata_ = true;
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void TearDown() override { cleanupOobStreams(); }

  void initializeConfig(absl::string_view policy_yaml, std::vector<std::string> remote_zones) {
    setUpstreamCount(2 * (1 + remote_zones.size()));
    const auto ip_version = GetParam();
    config_helper_.addConfigModifier(
        [ip_version, policy_yaml = std::string(policy_yaml),
         remote_zones](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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
          TestUtility::loadFromYaml(policy_yaml, *cluster->mutable_load_balancing_policy());
        });
    HttpIntegrationTest::initialize();
  }

  // Accept one OOB stream on the given upstream and reply with one
  // server-streamed OrcaLoadReport carrying `utilization`. Connection and
  // stream stay open so the session remains active.
  void acceptOobStream(size_t upstream_index, double utilization) {
    FakeHttpConnectionPtr conn;
    ASSERT_TRUE(fake_upstreams_[upstream_index]->waitForHttpConnection(*dispatcher_, conn));
    FakeStreamPtr stream;
    ASSERT_TRUE(conn->waitForNewStream(*dispatcher_, stream));
    ASSERT_TRUE(stream->waitForEndStream(*dispatcher_));

    Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                                 {"content-type", "application/grpc"}};
    stream->encodeHeaders(resp_headers, false);

    xds::data::orca::v3::OrcaLoadReport report;
    report.set_application_utilization(utilization);
    auto frame = Grpc::Common::serializeToGrpcFrame(report);
    stream->encodeData(*frame, false);

    stream_holder_.push_back(std::move(stream));
    conn_holder_.push_back(std::move(conn));
  }

  void cleanupOobStreams() {
    for (auto& conn : conn_holder_) {
      if (conn != nullptr) {
        AssertionResult result = conn->close();
        RELEASE_ASSERT(result, result.message());
        result = conn->waitForDisconnect();
        RELEASE_ASSERT(result, result.message());
      }
    }
    stream_holder_.clear();
    conn_holder_.clear();
  }

  std::vector<FakeHttpConnectionPtr> conn_holder_;
  std::vector<FakeStreamPtr> stream_holder_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LoadAwareLocalityOobIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// With OOB enabled on the locality policy alone, one stream opens per host and
// server-pushed reports alone -- with no downstream request traffic at all --
// drive the locality weight computation and the spill decision.
TEST_P(LoadAwareLocalityOobIntegrationTest, OobReportsDriveLocalityWeights) {
  initializeConfig(kOobWithRoundRobinChildYaml, /*remote_zones=*/{"zone-b"});

  // Upstreams 0-1 are local zone-a, 2-3 are remote zone-b.
  acceptOobStream(0, /*utilization=*/0.9);
  acceptOobStream(1, /*utilization=*/0.9);
  acceptOobStream(2, /*utilization=*/0.1);
  acceptOobStream(3, /*utilization=*/0.1);

  test_server_->waitForGauge("cluster.cluster_0.lb_orca_oob.active_sessions", testing::Eq(4));
  test_server_->waitForCounter("cluster.cluster_0.lb_orca_oob.reports_received", testing::Ge(4));
  test_server_->waitForCounter("cluster.cluster_0.load_aware_locality.spill_active_total",
                               testing::Ge(1));
}

// Documentation-via-test for #45725 (r3658010538): when the child endpoint
// policy ALSO enables OOB, each level runs its own manager, so every host
// receives two independent streams (2 hosts x 2 managers = 4 sessions), and
// the cluster-scoped lb_orca_oob.* stats compose across both managers. See
// SingleOobStreamFeedsBothPolicies below for proof that report fan-out still
// reaches both policies when only one level opens a stream -- the
// configuration the docs recommend.
TEST_P(LoadAwareLocalityOobIntegrationTest, BothLevelsOpenIndependentStreams) {
  initializeConfig(kOobBothLevelsYaml, /*remote_zones=*/{});

  // Two connections arrive per upstream, one from each manager.
  acceptOobStream(0, /*utilization=*/0.5);
  acceptOobStream(0, /*utilization=*/0.5);
  acceptOobStream(1, /*utilization=*/0.5);
  acceptOobStream(1, /*utilization=*/0.5);

  test_server_->waitForGauge("cluster.cluster_0.lb_orca_oob.active_sessions", testing::Eq(4));
  test_server_->waitForCounter("cluster.cluster_0.lb_orca_oob.reports_received", testing::Ge(4));
}

// Fan-out proof: enabling OOB at ONE level (load_aware_locality) still feeds
// BOTH consumers even though the ClientSideWeightedRoundRobin child opens no stream of its own --
// this is also the configuration the docs recommend.
TEST_P(LoadAwareLocalityOobIntegrationTest, SingleOobStreamFeedsBothPolicies) {
  initializeConfig(kOobLalOnlyCswrrChildYaml, /*remote_zones=*/{"zone-b"});

  // Upstreams 0-1 are local zone-a, 2-3 are remote zone-b. Reports carry only
  // application_utilization (no rps_fractional): LAL's consumer accepts that,
  // ClientSideWeightedRoundRobin's consumer does not.
  acceptOobStream(0, /*utilization=*/0.9);
  acceptOobStream(1, /*utilization=*/0.9);
  acceptOobStream(2, /*utilization=*/0.1);
  acceptOobStream(3, /*utilization=*/0.1);

  // One manager x 4 hosts: proves the ClientSideWeightedRoundRobin child opened no streams of its own.
  test_server_->waitForGauge("cluster.cluster_0.lb_orca_oob.active_sessions", testing::Eq(4));

  // report_errors increments only when a recipient's onOrcaLoadReport() returns
  // non-OK (orca_oob_manager.cc:396-404). LAL's LocalityLbHostData::
  // onOrcaLoadReport always returns OkStatus (load_aware_locality_lb.cc:24-34).
  // ClientSideWeightedRoundRobin's OrcaHostLbPolicyData::onOrcaLoadReport rejects any report with
  // rps_fractional <= 0 ("QPS must be positive", orca_weight_manager.cc:81-84).
  // So these errors can only be coming from the ClientSideWeightedRoundRobin child's consumer -- which
  // proves reports that arrived on LAL's stream were fanned out to the ClientSideWeightedRoundRobin
  // child's host data even though ClientSideWeightedRoundRobin opened no stream of its own.
  test_server_->waitForCounter("cluster.cluster_0.lb_orca_oob.report_errors", testing::Ge(4));

  // Proves the load_aware_locality consumer received the same reports.
  test_server_->waitForCounter("cluster.cluster_0.load_aware_locality.spill_active_total",
                               testing::Ge(1));
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
