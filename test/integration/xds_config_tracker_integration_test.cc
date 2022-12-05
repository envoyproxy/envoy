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

constexpr char XDS_CLUSTER_NAME[] = "xds_cluster";

/**
 * All stats for this xds tracker. @see stats_macros.h
 */
#define ALL_TEST_XDS_TRACKER_STATS(COUNTER)                                                        \
  COUNTER(on_config_accepted)                                                                      \
  COUNTER(on_config_rejected)

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
                        const std::vector<Config::DecodedResourcePtr>&) override {
    stats_.on_config_accepted_.inc();
  }

  void onConfigAccepted(const absl::string_view,
                        const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>&,
                        const Protobuf::RepeatedPtrField<std::string>&) override {
    stats_.on_config_accepted_.inc();
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
                                                     Event::Dispatcher&,
                                                     Stats::Scope& stats) override {
    return std::make_unique<TestXdsConfigTracker>(stats);
  }
};

class XdsConfigTrackerIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                        public HttpIntegrationTest {
public:
  XdsConfigTrackerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::baseConfigNoListeners()) {

    use_lds_ = false;
    create_xds_upstream_ = true;
    sotw_or_delta_ = sotwOrDelta();

    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta ||
                                       this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                          ? "true"
                                          : "false");

    // Make the default cluster HTTP2.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::setHttp2(*bootstrap.mutable_static_resources()->mutable_clusters(0));
    });

    // Build and add the xDS cluster config.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->MergeFrom(ConfigHelper::buildStaticCluster(
          std::string(XDS_CLUSTER_NAME),
          /*port=*/0, ipVersion() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1"));
      ConfigHelper::setHttp2(*xds_cluster);
    });

    // Add static runtime values.
    config_helper_.addRuntimeOverride("whatevs", "yar");

    // Set up the RTDS runtime layer.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
      layer->set_name("some_rtds_layer");
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name("some_rtds_layer");
      auto* rtds_config = rtds_layer->mutable_rtds_config();
      rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = rtds_config->mutable_api_config_source();
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      api_config_source->set_api_type((this->sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                       this->sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw)
                                          ? envoy::config::core::v3::ApiConfigSource::GRPC
                                          : envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
      api_config_source->set_set_node_on_first_message_only(true);
      api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
          XDS_CLUSTER_NAME);
    });

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
    registerTestServerPorts({});
  }

  void acceptXdsConnection() {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, XdsConfigTrackerIntegrationTest,
                         DELTA_SOTW_UNIFIED_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerSuccessCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  const auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounterEq("test_xds_tracker.on_config_accepted", 1);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_accepted")->value());
}

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerFailureCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
    name: my_route
    vhds:
      config_source:
        resource_api_version: V3
        api_config_source:
          api_type: GRPC
          transport_api_version: V3
          grpc_services:
            envoy_grpc:
              cluster_name: xds_cluster
    )EOF");

  // Message's TypeUrl != Resource's
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().Runtime, {route_config}, {route_config}, {}, "1");

  test_server_->waitForCounterEq("test_xds_tracker.on_config_rejected", 1);
  EXPECT_EQ(1, test_server_->counter("test_xds_tracker.on_config_rejected")->value());
}

} // namespace
} // namespace Envoy
