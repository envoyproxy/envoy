#include <atomic>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/xds_config_tracker_test.pb.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

const char kTestKey[] = "test_key";
const char ClusterName1[] = "cluster_1";
const char ClusterName2[] = "cluster_2";
const int UpstreamIndex1 = 1;
const int UpstreamIndex2 = 2;

/**
 * All stats for this xds tracker. @see stats_macros.h
 */
#define ALL_TEST_XDS_TRACKER_STATS(COUNTER)                                                        \
  COUNTER(on_config_accepted)                                                                      \
  COUNTER(on_config_rejected)                                                                      \
  COUNTER(on_config_metadata_read)

/**
 * Struct definition for stats. @see stats_macros.h
 */
struct TestXdsTrackerStats {
  ALL_TEST_XDS_TRACKER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * A test implementation of the XdsConfigTracker extension.
 * It just increases the test counter when a related method is called.
 */
class TestXdsConfigTracker : public Config::XdsConfigTracker {
public:
  TestXdsConfigTracker(Stats::Scope& scope) : stats_(generateStats("test_xds_tracker", scope)) {}

  void onConfigAccepted(const absl::string_view,
                        const std::vector<Config::DecodedResourcePtr>& decoded_resources) override {
    stats_.on_config_accepted_.inc();
    test::envoy::config::xds::TestTrackerMetadata test_metadata;
    for (const auto& resource : decoded_resources) {
      if (resource->metadata().has_value()) {
        const auto& config_typed_metadata = resource->metadata()->typed_filter_metadata();
        if (const auto& metadata_it = config_typed_metadata.find(kTestKey);
            metadata_it != config_typed_metadata.end()) {
          const auto status = Envoy::MessageUtil::unpackTo(metadata_it->second, test_metadata);
          if (!status.ok()) {
            continue;
          }
          stats_.on_config_metadata_read_.inc();
        }
      }
    }
  }

  void onConfigAccepted(const absl::string_view,
                        absl::Span<const envoy::service::discovery::v3::Resource* const> resources,
                        const Protobuf::RepeatedPtrField<std::string>&) override {
    stats_.on_config_accepted_.inc();
    test::envoy::config::xds::TestTrackerMetadata test_metadata;
    for (const auto* resource : resources) {
      if (resource->has_metadata()) {
        const auto& config_typed_metadata = resource->metadata().typed_filter_metadata();
        if (const auto& metadata_it = config_typed_metadata.find(kTestKey);
            metadata_it != config_typed_metadata.end()) {
          const auto status = Envoy::MessageUtil::unpackTo(metadata_it->second, test_metadata);
          if (!status.ok()) {
            continue;
          }
          stats_.on_config_metadata_read_.inc();
        }
      }
    }
  }

  void onConfigRejected(const envoy::service::discovery::v3::DiscoveryResponse&,
                        const absl::string_view) override {
    stats_.on_config_rejected_.inc();
  }

  void onConfigRejected(const envoy::service::discovery::v3::DeltaDiscoveryResponse&,
                        const absl::string_view) override {
    stats_.on_config_rejected_.inc();
  }

private:
  TestXdsTrackerStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return {ALL_TEST_XDS_TRACKER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
  TestXdsTrackerStats stats_;
};

class TestXdsConfigTrackerFactory : public Config::XdsConfigTrackerFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::envoy::config::xds::TestXdsConfigTracker>();
  }

  std::string name() const override { return "envoy.config.xds.test_xds_tracker"; };

  Config::XdsConfigTrackerPtr createXdsConfigTracker(const ProtobufWkt::Any&,
                                                     ProtobufMessage::ValidationVisitor&,
                                                     Api::Api& api, Event::Dispatcher&) override {
    return std::make_unique<TestXdsConfigTracker>(api.rootScope());
  }
};

class XdsConfigTrackerIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                        public HttpIntegrationTest {
public:
  XdsConfigTrackerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::clustersNoListenerBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                        sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
                                    ? "GRPC"
                                    : "DELTA_GRPC")) {

    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();

    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta ||
                                       this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                          ? "true"
                                          : "false");

    // Add test xDS config tracer.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* tracer_extension = bootstrap.mutable_xds_config_tracker_extension();
      tracer_extension->set_name("envoy.config.xds.test_xds_tracer");
      tracer_extension->mutable_typed_config()->PackFrom(
          test::envoy::config::xds::TestXdsConfigTracker());
    });
  }

  void TearDown() override {
    if (xds_connection_ != nullptr) {
      cleanUpXdsConnection();
    }
  }

  void initialize() override {
    // The tests infra expects the xDS server to be the second fake upstream, so
    // we need a dummy data plane cluster.
    setUpstreamCount(1);
    setUpstreamProtocol(Http::CodecType::HTTP2);
    HttpIntegrationTest::initialize();

    // Create the regular (i.e. not an xDS server) upstreams.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP2);
    cluster1_ = ConfigHelper::buildStaticCluster(
        ClusterName1, fake_upstreams_[UpstreamIndex1]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));
    cluster2_ = ConfigHelper::buildStaticCluster(
        ClusterName2, fake_upstreams_[UpstreamIndex2]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    acceptXdsConnection();
    registerTestServerPorts({});
  }

  void acceptXdsConnection() {
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  envoy::config::cluster::v3::Cluster cluster1_;
  envoy::config::cluster::v3::Cluster cluster2_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, XdsConfigTrackerIntegrationTest,
                         DELTA_SOTW_UNIFIED_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerSuccessCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));

  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster1_, cluster2_}, {}, "1");

  // 3 because the statically specified CDS server itself counts as a cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // onConfigAccepted is called when all the resources are accepted.
  test_server_->waitForCounterEq("test_xds_tracker.on_config_accepted", 1);
  test_server_->waitForCounterEq("test_xds_tracker.on_config_metadata_read", 0);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_accepted")->value());
}

// This is to test the Resource wrapper usage with metadata field.
TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerSuccessCountWithWrapper) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));

  // Add a typed metadata to the Resource wrapper.
  test::envoy::config::xds::TestTrackerMetadata test_metadata;
  ProtobufWkt::Any packed_value;
  packed_value.PackFrom(test_metadata);
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster2_}, {cluster1_, cluster2_}, {}, "1",
      {{kTestKey, packed_value}});

  // 3 because the statically specified CDS server itself counts as a cluster.
  test_server_->waitForGaugeGe("cluster_manager.active_clusters", 3);

  // onConfigAccepted is called when all the resources are accepted.
  test_server_->waitForCounterEq("test_xds_tracker.on_config_accepted", 1);
  test_server_->waitForCounterEq("test_xds_tracker.on_config_metadata_read", 2);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_accepted")->value());
}

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerFailureCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
    name: my_route
    vhds:
      config_source:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: xds_cluster
    )EOF");

  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().Cluster, {route_config}, {route_config}, {}, "3");

  // Resources are rejected because Message's TypeUrl != Resource's
  test_server_->waitForCounterEq("test_xds_tracker.on_config_rejected", 1);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_rejected")->value());
}

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerPartialUpdate) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  // The first of duplicates has already been successfully applied,
  // and a duplicate exception should be threw.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {cluster1_, cluster1_, cluster2_},
      {cluster1_, cluster1_, cluster2_}, {}, "5");

  // For Delta, the response will be rejected when checking the message due to the duplication.
  // For SotW, both clusters are accepted, but the internal exception is not empty.
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Delta ||
      sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedDelta) {
    test_server_->waitForCounterGe("cluster_manager.cluster_added", 1);
  } else {
    test_server_->waitForCounterGe("cluster_manager.cluster_added", 3);
  }

  // onConfigRejected is called if there is any exception even some resources are accepted.
  test_server_->waitForCounterEq("test_xds_tracker.on_config_rejected", 1);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_rejected")->value());

  // onConfigAccepted is called only when all the resources in a response are successfully ingested.
  EXPECT_EQ(0, test_server_->counter("test_xds_tracker.on_config_accepted")->value());
}

} // namespace
} // namespace Envoy
