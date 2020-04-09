#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/endpoint/v3/load_report.pb.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"

#include "common/config/resources.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LoadStatsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  LoadStatsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    // We rely on some fairly specific load balancing picks in this test, so
    // determinize the schedule.
    setDeterministic();
  }

  void addEndpoint(envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoints,
                   uint32_t index, uint32_t& num_endpoints) {
    setUpstreamAddress(index + 1, *locality_lb_endpoints.add_lb_endpoints());
    ++num_endpoints;
  }

  // Used as args to updateClusterLocalityAssignment().
  struct LocalityAssignment {
    LocalityAssignment() : LocalityAssignment({}, 0) {}
    LocalityAssignment(const std::vector<uint32_t>& endpoints) : LocalityAssignment(endpoints, 0) {}
    LocalityAssignment(const std::vector<uint32_t>& endpoints, uint32_t weight)
        : endpoints_(endpoints), weight_(weight) {}

    // service_upstream_ indices for endpoints in the cluster.
    const std::vector<uint32_t> endpoints_;
    // If non-zero, locality level weighting.
    const uint32_t weight_{};
  };

  // We need to supply the endpoints via EDS to provide locality information for
  // load reporting. Use a filesystem delivery to simplify test mechanics.
  void updateClusterLoadAssignment(const LocalityAssignment& winter_upstreams,
                                   const LocalityAssignment& dragon_upstreams,
                                   const LocalityAssignment& p1_winter_upstreams,
                                   const LocalityAssignment& p1_dragon_upstreams) {
    uint32_t num_endpoints = 0;
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
    // EDS service_name is set in cluster_0
    cluster_load_assignment.set_cluster_name("service_name_0");

    auto* winter = cluster_load_assignment.add_endpoints();
    winter->mutable_locality()->set_region("some_region");
    winter->mutable_locality()->set_zone("zone_name");
    winter->mutable_locality()->set_sub_zone("winter");
    if (winter_upstreams.weight_ > 0) {
      winter->mutable_load_balancing_weight()->set_value(winter_upstreams.weight_);
    }
    for (uint32_t index : winter_upstreams.endpoints_) {
      addEndpoint(*winter, index, num_endpoints);
    }

    auto* dragon = cluster_load_assignment.add_endpoints();
    dragon->mutable_locality()->set_region("some_region");
    dragon->mutable_locality()->set_zone("zone_name");
    dragon->mutable_locality()->set_sub_zone("dragon");
    if (dragon_upstreams.weight_ > 0) {
      dragon->mutable_load_balancing_weight()->set_value(dragon_upstreams.weight_);
    }
    for (uint32_t index : dragon_upstreams.endpoints_) {
      addEndpoint(*dragon, index, num_endpoints);
    }

    auto* winter_p1 = cluster_load_assignment.add_endpoints();
    winter_p1->set_priority(1);
    winter_p1->mutable_locality()->set_region("some_region");
    winter_p1->mutable_locality()->set_zone("zone_name");
    winter_p1->mutable_locality()->set_sub_zone("winter");
    for (uint32_t index : p1_winter_upstreams.endpoints_) {
      addEndpoint(*winter_p1, index, num_endpoints);
    }

    auto* dragon_p1 = cluster_load_assignment.add_endpoints();
    dragon_p1->set_priority(1);
    dragon_p1->mutable_locality()->set_region("some_region");
    dragon_p1->mutable_locality()->set_zone("zone_name");
    dragon_p1->mutable_locality()->set_sub_zone("dragon");
    for (uint32_t index : p1_dragon_upstreams.endpoints_) {
      addEndpoint(*dragon_p1, index, num_endpoints);
    }
    eds_helper_.setEdsAndWait({cluster_load_assignment}, *test_server_);
  }

  void createUpstreams() override {
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
    load_report_upstream_ = fake_upstreams_.back().get();
    HttpIntegrationTest::createUpstreams();
  }

  void initialize() override {
    setUpstreamCount(upstream_endpoints_);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Setup load reporting and corresponding gRPC cluster.
      auto* loadstats_config = bootstrap.mutable_cluster_manager()->mutable_load_stats_config();
      loadstats_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      loadstats_config->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("load_report");
      auto* load_report_cluster = bootstrap.mutable_static_resources()->add_clusters();
      load_report_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      load_report_cluster->mutable_circuit_breakers()->Clear();
      load_report_cluster->set_name("load_report");
      load_report_cluster->mutable_http2_protocol_options();
      // Put ourselves in a locality that will be used in
      // updateClusterLoadAssignment()
      auto* locality = bootstrap.mutable_node()->mutable_locality();
      locality->set_region("some_region");
      locality->set_zone("zone_name");
      locality->set_sub_zone(sub_zone_);
      // Switch predefined cluster_0 to EDS filesystem sourcing.
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->set_type(envoy::config::cluster::v3::Cluster::EDS);
      auto* eds_cluster_config = cluster_0->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_path(eds_helper_.eds_path());
      eds_cluster_config->set_service_name("service_name_0");
      if (locality_weighted_lb_) {
        cluster_0->mutable_common_lb_config()->mutable_locality_weighted_lb_config();
      }
    });
    HttpIntegrationTest::initialize();
    load_report_upstream_ = fake_upstreams_[0].get();
    for (uint32_t i = 0; i < upstream_endpoints_; ++i) {
      service_upstream_[i] = fake_upstreams_[i + 1].get();
    }
    updateClusterLoadAssignment({}, {}, {}, {});
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
        {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
    response_ = codec_client_->makeRequestWithBody(headers, request_size_);
  }

  void waitForLoadStatsStream() {
    AssertionResult result =
        load_report_upstream_->waitForHttpConnection(*dispatcher_, fake_loadstats_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_loadstats_connection_->waitForNewStream(*dispatcher_, loadstats_stream_);
    RELEASE_ASSERT(result, result.message());
  }

  void
  mergeLoadStats(envoy::service::load_stats::v3::LoadStatsRequest& loadstats_request,
                 const envoy::service::load_stats::v3::LoadStatsRequest& local_loadstats_request) {
    ASSERT(loadstats_request.cluster_stats_size() <= 1);
    ASSERT(local_loadstats_request.cluster_stats_size() <= 1);

    if (local_loadstats_request.cluster_stats_size() == 0) {
      return;
    } else if (loadstats_request.cluster_stats_size() == 0) {
      loadstats_request.CopyFrom(local_loadstats_request);
      ASSERT_TRUE(loadstats_request.has_node());
      ASSERT_FALSE(loadstats_request.node().id().empty());
      ASSERT_FALSE(loadstats_request.node().cluster().empty());
      return;
    }

    const auto& local_cluster_stats = local_loadstats_request.cluster_stats(0);
    auto* cluster_stats = loadstats_request.mutable_cluster_stats(0);

    cluster_stats->set_total_dropped_requests(cluster_stats->total_dropped_requests() +
                                              local_cluster_stats.total_dropped_requests());

    for (int i = 0; i < local_cluster_stats.upstream_locality_stats_size(); ++i) {
      const auto& local_upstream_locality_stats = local_cluster_stats.upstream_locality_stats(i);
      bool copied = false;
      for (int j = 0; j < cluster_stats->upstream_locality_stats_size(); ++j) {
        auto* upstream_locality_stats = cluster_stats->mutable_upstream_locality_stats(j);
        if (TestUtility::protoEqual(upstream_locality_stats->locality(),
                                    local_upstream_locality_stats.locality()) &&
            upstream_locality_stats->priority() == local_upstream_locality_stats.priority()) {
          copied = true;
          upstream_locality_stats->set_total_successful_requests(
              upstream_locality_stats->total_successful_requests() +
              local_upstream_locality_stats.total_successful_requests());
          upstream_locality_stats->set_total_error_requests(
              upstream_locality_stats->total_error_requests() +
              local_upstream_locality_stats.total_error_requests());
          upstream_locality_stats->set_total_issued_requests(
              upstream_locality_stats->total_issued_requests() +
              local_upstream_locality_stats.total_issued_requests());
          // Unlike most stats, current requests in progress replaces old requests in progress.
          break;
        }
      }
      if (!copied) {
        auto* upstream_locality_stats = cluster_stats->add_upstream_locality_stats();
        upstream_locality_stats->CopyFrom(local_upstream_locality_stats);
      }
    }

    // Unfortunately because we don't issue an update when total_requests_in_progress goes from
    // non-zero to zero, we have to go through and zero it out for any locality stats we didn't see.
    for (int i = 0; i < cluster_stats->upstream_locality_stats_size(); ++i) {
      auto upstream_locality_stats = cluster_stats->mutable_upstream_locality_stats(i);
      bool found = false;
      for (int j = 0; j < local_cluster_stats.upstream_locality_stats_size(); ++j) {
        auto& local_upstream_locality_stats = local_cluster_stats.upstream_locality_stats(j);
        if (TestUtility::protoEqual(upstream_locality_stats->locality(),
                                    local_upstream_locality_stats.locality()) &&
            upstream_locality_stats->priority() == local_upstream_locality_stats.priority()) {
          found = true;
          break;
        }
      }
      if (!found) {
        upstream_locality_stats->set_total_requests_in_progress(0);
      }
    }
  }

  void
  waitForLoadStatsRequest(const std::vector<envoy::config::endpoint::v3::UpstreamLocalityStats>&
                              expected_locality_stats,
                          uint64_t dropped = 0) {
    Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats;
    if (!expected_locality_stats.empty() || dropped != 0) {
      auto* cluster_stats = expected_cluster_stats.Add();
      cluster_stats->set_cluster_name("cluster_0");
      // Verify the eds service_name is passed back.
      cluster_stats->set_cluster_service_name("service_name_0");
      if (dropped > 0) {
        cluster_stats->set_total_dropped_requests(dropped);
      }
      std::copy(
          expected_locality_stats.begin(), expected_locality_stats.end(),
          Protobuf::RepeatedPtrFieldBackInserter(cluster_stats->mutable_upstream_locality_stats()));
    }

    envoy::service::load_stats::v3::LoadStatsRequest loadstats_request;
    // Because multiple load stats may be sent while load in being sent (on slow machines), loop and
    // merge until all the expected load has been reported.
    do {
      envoy::service::load_stats::v3::LoadStatsRequest local_loadstats_request;
      AssertionResult result =
          loadstats_stream_->waitForGrpcMessage(*dispatcher_, local_loadstats_request);
      RELEASE_ASSERT(result, result.message());
      // Sanity check and clear the measured load report interval.
      for (auto& cluster_stats : *local_loadstats_request.mutable_cluster_stats()) {
        const uint32_t actual_load_report_interval_ms =
            Protobuf::util::TimeUtil::DurationToMilliseconds(cluster_stats.load_report_interval());
        // Turns out libevent timers aren't that accurate; without this adjustment we see things
        // like "expected 500, actual 497". Tweak as needed if races are observed.
        EXPECT_GE(actual_load_report_interval_ms, load_report_interval_ms_ - 100);
        // Allow for some skew in test environment.
        EXPECT_LT(actual_load_report_interval_ms, load_report_interval_ms_ + 1000);
        cluster_stats.mutable_load_report_interval()->Clear();
      }
      mergeLoadStats(loadstats_request, local_loadstats_request);

      EXPECT_EQ("POST", loadstats_stream_->headers().Method()->value().getStringView());
      EXPECT_EQ("/envoy.service.load_stats.v2.LoadReportingService/StreamLoadStats",
                loadstats_stream_->headers().Path()->value().getStringView());
      EXPECT_EQ("application/grpc",
                loadstats_stream_->headers().ContentType()->value().getStringView());
    } while (!TestUtility::assertRepeatedPtrFieldEqual(expected_cluster_stats,
                                                       loadstats_request.cluster_stats(), true));
  }

  void waitForUpstreamResponse(uint32_t endpoint_index, uint32_t response_code = 200) {
    AssertionResult result = service_upstream_[endpoint_index]->waitForHttpConnection(
        *dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", std::to_string(response_code)}}, false);
    upstream_request_->encodeData(response_size_, true);
    response_->waitForEndStream();

    ASSERT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    ASSERT_TRUE(response_->complete());
    EXPECT_EQ(std::to_string(response_code),
              response_->headers().Status()->value().getStringView());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void requestLoadStatsResponse(const std::vector<std::string>& clusters) {
    envoy::service::load_stats::v3::LoadStatsResponse loadstats_response;
    loadstats_response.mutable_load_reporting_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(load_report_interval_ms_));
    for (const auto& cluster : clusters) {
      loadstats_response.add_clusters(cluster);
    }
    loadstats_stream_->sendGrpcMessage(loadstats_response);
    // Wait until the request has been received by Envoy.
    test_server_->waitForCounterGe("load_reporter.requests", ++load_requests_);
  }

  envoy::config::endpoint::v3::UpstreamLocalityStats localityStats(const std::string& sub_zone,
                                                                   uint64_t success, uint64_t error,
                                                                   uint64_t active, uint64_t issued,
                                                                   uint32_t priority = 0) {
    envoy::config::endpoint::v3::UpstreamLocalityStats locality_stats;
    auto* locality = locality_stats.mutable_locality();
    locality->set_region("some_region");
    locality->set_zone("zone_name");
    locality->set_sub_zone(sub_zone);
    locality_stats.set_total_successful_requests(success);
    locality_stats.set_total_error_requests(error);
    locality_stats.set_total_requests_in_progress(active);
    locality_stats.set_total_issued_requests(issued);
    locality_stats.set_priority(priority);
    return locality_stats;
  }

  void cleanupLoadStatsConnection() {
    if (fake_loadstats_connection_ != nullptr) {
      AssertionResult result = fake_loadstats_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_loadstats_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  void sendAndReceiveUpstream(uint32_t endpoint_index, uint32_t response_code = 200) {
    initiateClientConnection();
    waitForUpstreamResponse(endpoint_index, response_code);
    cleanupUpstreamAndDownstream();
  }

  static constexpr uint32_t upstream_endpoints_ = 5;

  IntegrationStreamDecoderPtr response_;
  std::string sub_zone_{"winter"};
  FakeHttpConnectionPtr fake_loadstats_connection_;
  FakeStreamPtr loadstats_stream_;
  FakeUpstream* load_report_upstream_{};
  FakeUpstream* service_upstream_[upstream_endpoints_]{};
  uint32_t load_requests_{};
  EdsHelper eds_helper_;
  bool locality_weighted_lb_{};

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
  const uint32_t load_report_interval_ms_ = 500;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LoadStatsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate the load reports for successful requests as cluster membership
// changes.
TEST_P(LoadStatsIntegrationTest, Success) {
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});
  loadstats_stream_->startGrpcStream();

  // Simple 50%/50% split between dragon/winter localities. Also include an
  // unknown cluster to exercise the handling of this case.
  requestLoadStatsResponse({"cluster_0", "cluster_1"});

  updateClusterLoadAssignment({{0}}, {{1}}, {{3}}, {});

  for (uint32_t i = 0; i < 4; ++i) {
    sendAndReceiveUpstream(i % 2);
  }

  // Verify we do not get empty stats for non-zero priorities.
  waitForLoadStatsRequest(
      {localityStats("winter", 2, 0, 0, 2), localityStats("dragon", 2, 0, 0, 2)});

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  // On slow machines, more than one load stats response may be pushed while we are simulating load.
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  // 33%/67% split between dragon/winter primary localities.
  updateClusterLoadAssignment({{0}}, {{1, 2}}, {}, {{4}});
  requestLoadStatsResponse({"cluster_0"});

  for (uint32_t i = 0; i < 6; ++i) {
    sendAndReceiveUpstream((4 + i) % 3);
  }

  // No locality for priority=1 since there's no "winter" endpoints.
  // The hosts for dragon were received because membership_total is accurate.
  waitForLoadStatsRequest(
      {localityStats("winter", 2, 0, 0, 2), localityStats("dragon", 4, 0, 0, 4)});

  EXPECT_EQ(2, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(3, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  // Change to 50/50 for the failover clusters.
  updateClusterLoadAssignment({}, {}, {{3}}, {{4}});
  requestLoadStatsResponse({"cluster_0"});
  test_server_->waitForGaugeEq("cluster.cluster_0.membership_total", 2);

  for (uint32_t i = 0; i < 4; ++i) {
    sendAndReceiveUpstream(i % 2 + 3);
  }

  waitForLoadStatsRequest(
      {localityStats("winter", 2, 0, 0, 2, 1), localityStats("dragon", 2, 0, 0, 2, 1)});
  EXPECT_EQ(3, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(4, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  // 100% winter locality.
  updateClusterLoadAssignment({}, {}, {}, {});
  updateClusterLoadAssignment({{1}}, {}, {}, {});
  requestLoadStatsResponse({"cluster_0"});

  for (uint32_t i = 0; i < 1; ++i) {
    sendAndReceiveUpstream(1);
  }

  waitForLoadStatsRequest({localityStats("winter", 1, 0, 0, 1)});
  EXPECT_EQ(4, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(5, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  // A LoadStatsResponse arrives before the expiration of the reporting
  // interval. Since we are keep tracking cluster_0, stats rollover.
  requestLoadStatsResponse({"cluster_0"});
  sendAndReceiveUpstream(1);
  requestLoadStatsResponse({"cluster_0"});
  sendAndReceiveUpstream(1);
  sendAndReceiveUpstream(1);

  waitForLoadStatsRequest({localityStats("winter", 3, 0, 0, 3)});

  EXPECT_EQ(6, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(6, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  // As above, but stop tracking cluster_0 and only get the requests since the
  // response.
  requestLoadStatsResponse({});
  sendAndReceiveUpstream(1);
  requestLoadStatsResponse({"cluster_0"});
  sendAndReceiveUpstream(1);
  sendAndReceiveUpstream(1);

  waitForLoadStatsRequest({localityStats("winter", 2, 0, 0, 2)});

  EXPECT_EQ(8, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(7, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

// Validate the load reports for successful requests when using locality
// weighted LB. This serves as a de facto integration test for locality weighted
// LB.
TEST_P(LoadStatsIntegrationTest, LocalityWeighted) {
  locality_weighted_lb_ = true;
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});

  loadstats_stream_->startGrpcStream();
  requestLoadStatsResponse({"cluster_0"});

  // Simple 33%/67% split between dragon/winter localities.
  // Even though there are more endpoints in the dragon locality, the winter locality gets the
  // expected weighting in the WRR locality schedule.
  updateClusterLoadAssignment({{0}, 2}, {{1, 2}, 1}, {}, {});

  sendAndReceiveUpstream(0);
  sendAndReceiveUpstream(1);
  sendAndReceiveUpstream(0);
  sendAndReceiveUpstream(0);
  sendAndReceiveUpstream(2);
  sendAndReceiveUpstream(0);

  // Verify we get the expect request distribution.
  waitForLoadStatsRequest(
      {localityStats("winter", 4, 0, 0, 4), localityStats("dragon", 2, 0, 0, 2)});

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  // On slow machines, more than one load stats response may be pushed while we are simulating load.
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

// Validate the load reports for requests when all endpoints are non-local.
TEST_P(LoadStatsIntegrationTest, NoLocalLocality) {
  sub_zone_ = "summer";
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});
  loadstats_stream_->startGrpcStream();

  // Simple 50%/50% split between dragon/winter localities. Also include an
  // unknown cluster to exercise the handling of this case.
  requestLoadStatsResponse({"cluster_0", "cluster_1"});

  updateClusterLoadAssignment({{0}}, {{1}}, {{3}}, {});

  for (uint32_t i = 0; i < 4; ++i) {
    sendAndReceiveUpstream(i % 2);
  }

  // Verify we do not get empty stats for non-zero priorities. Note that the
  // order of locality stats is different to the Success case, where winter is
  // the local locality (and hence first in the list as per
  // HostsPerLocality::get()).
  waitForLoadStatsRequest(
      {localityStats("dragon", 2, 0, 0, 2), localityStats("winter", 2, 0, 0, 2)});

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  // On slow machines, more than one load stats response may be pushed while we are simulating load.
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

// Validate the load reports for successful/error requests make sense.
TEST_P(LoadStatsIntegrationTest, Error) {
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});
  loadstats_stream_->startGrpcStream();

  requestLoadStatsResponse({"cluster_0"});
  updateClusterLoadAssignment({{0}}, {}, {}, {});

  // This should count as an error since 5xx.
  sendAndReceiveUpstream(0, 503);

  // This should count as "success" since non-5xx.
  sendAndReceiveUpstream(0, 404);

  waitForLoadStatsRequest({localityStats("winter", 1, 1, 0, 2)});

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

// Validate the load reports for in-progress make sense.
TEST_P(LoadStatsIntegrationTest, InProgress) {
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});
  loadstats_stream_->startGrpcStream();
  updateClusterLoadAssignment({{0}}, {}, {}, {});

  requestLoadStatsResponse({"cluster_0"});
  initiateClientConnection();
  waitForLoadStatsRequest({localityStats("winter", 0, 0, 1, 1)});

  waitForUpstreamResponse(0, 503);
  cleanupUpstreamAndDownstream();

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

// Validate the load reports for dropped requests make sense.
TEST_P(LoadStatsIntegrationTest, Dropped) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* thresholds = cluster_0->mutable_circuit_breakers()->add_thresholds();
    thresholds->mutable_max_pending_requests()->set_value(0);
  });
  initialize();

  waitForLoadStatsStream();
  waitForLoadStatsRequest({});
  loadstats_stream_->startGrpcStream();

  updateClusterLoadAssignment({{0}}, {}, {}, {});
  requestLoadStatsResponse({"cluster_0"});
  // This should count as dropped, since we trigger circuit breaking.
  initiateClientConnection();
  response_->waitForEndStream();
  ASSERT_TRUE(response_->complete());
  EXPECT_EQ("503", response_->headers().Status()->value().getStringView());
  cleanupUpstreamAndDownstream();

  waitForLoadStatsRequest({}, 1);

  EXPECT_EQ(1, test_server_->counter("load_reporter.requests")->value());
  EXPECT_LE(2, test_server_->counter("load_reporter.responses")->value());
  EXPECT_EQ(0, test_server_->counter("load_reporter.errors")->value());

  cleanupLoadStatsConnection();
}

} // namespace
} // namespace Envoy
