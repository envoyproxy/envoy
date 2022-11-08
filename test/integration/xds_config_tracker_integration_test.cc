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

using testing::HasSubstr;

namespace Envoy {
namespace {

constexpr char XDS_CLUSTER_NAME[] = "xds_cluster";

class TestXdsConfigTracker : public Config::XdsConfigTracker {
public:
  TestXdsConfigTracker() {
    ReceiveCount = 0;
    IngestedCount = 0;
    FailureCount = 0;
    ErrorMessage = "";
  }

  void onConfigIngested(const absl::string_view,
                        const std::vector<Config::DecodedResourcePtr>&) override {
    ++IngestedCount;
  }

  void onConfigIngested(const absl::string_view, const std::vector<Config::DecodedResourcePtr>&,
                        const Protobuf::RepeatedPtrField<std::string>&) override {
    ++IngestedCount;
  }

  void onResponseReceiveOrFail(const envoy::service::discovery::v3::DiscoveryResponse&,
                               const Config::ProcessingDetails& process_details) override {
    countState(process_details.state_);
    if (process_details.state_ == Config::ProcessingState::FAILED) {
      ErrorMessage = process_details.error_detail_.message();
    }
  }

  void onResponseReceiveOrFail(const envoy::service::discovery::v3::DeltaDiscoveryResponse&,
                               const Config::ProcessingDetails& process_details) override {
    countState(process_details.state_);
    if (process_details.state_ == Config::ProcessingState::FAILED) {
      ErrorMessage = process_details.error_detail_.message();
    }
  }

  static std::atomic<int> ReceiveCount;
  static std::atomic<int> IngestedCount;
  static std::atomic<int> FailureCount;
  static std::string ErrorMessage;

private:
  void countState(const Config::ProcessingState& state) {
    switch (state) {
    case Config::ProcessingState::RECEIVED:
      ++ReceiveCount;
      break;
    case Config::ProcessingState::FAILED:
      ++FailureCount;
      break;
    };
  }
};

std::atomic<int> TestXdsConfigTracker::ReceiveCount;
std::atomic<int> TestXdsConfigTracker::IngestedCount;
std::atomic<int> TestXdsConfigTracker::FailureCount;
std::string TestXdsConfigTracker::ErrorMessage;

static std::map<std::string, envoy::service::discovery::v3::Resource> ResourcesMap;

class TestXdsConfigTrackerFactory : public Config::XdsConfigTrackerFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::envoy::config::xds::TestXdsConfigTracker>();
  }

  std::string name() const override { return "envoy.config.xds.test_xds_tracer"; };

  Config::XdsConfigTrackerPtr createXdsConfigTracker(const ProtobufWkt::Any&,
                                                     ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<TestXdsConfigTracker>();
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
      api_config_source->set_api_type(this->sotwOrDelta() == Grpc::SotwOrDelta::Sotw
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
    initial_load_success_ = test_server_->counter("runtime.load_success")->value();
  }

  void acceptXdsConnection() {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }

  std::string getRuntimeKey(const std::string& key) {
    auto response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/runtime?format=json", "", downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(response->body());
    auto entries = loader->getObject("entries");
    if (entries->hasObject(key)) {
      return entries->getObject(key)->getString("final_value");
    }
    return "";
  }

  void waitforReceiveCount(const int expected_count) {
    absl::MutexLock l(&lock_);
    const auto reached_expected_count = [expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return TestXdsConfigTracker::IngestedCount == expected_count;
    };
    timeSystem().waitFor(lock_, absl::Condition(&reached_expected_count),
                         TestUtility::DefaultTimeout);
  }

  void waitforFailureCount(const int expected_count) {
    absl::MutexLock l(&lock_);
    const auto reached_expected_count = [expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return TestXdsConfigTracker::FailureCount >= expected_count;
    };
    timeSystem().waitFor(lock_, absl::Condition(&reached_expected_count),
                         TestUtility::DefaultTimeout);
  }

  absl::Mutex lock_;
  uint32_t initial_load_success_{0};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, XdsConfigTrackerIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerSuccessCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  int current_ingested_count = TestXdsConfigTracker::IngestedCount;
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 1);
  int expected_ingested_count = ++current_ingested_count;
  waitforReceiveCount(expected_ingested_count);

  EXPECT_EQ(expected_ingested_count, TestXdsConfigTracker::IngestedCount);
  EXPECT_EQ(expected_ingested_count, TestXdsConfigTracker::ReceiveCount);
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));
}

TEST_P(XdsConfigTrackerIntegrationTest, XdsConfigTrackerFailureCount) {
  TestXdsConfigTrackerFactory factory;
  Registry::InjectFactory<Config::XdsConfigTrackerFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: different_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");

  auto route_config = TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
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

  FakeStream* stream = xds_stream_.get();
  envoy::service::discovery::v3::DiscoveryResponse discovery_response;
  discovery_response.set_version_info("1");
  discovery_response.set_type_url(Config::TypeUrl::get().Runtime);
  discovery_response.add_resources()->PackFrom(route_config);

  static int next_nonce_counter = 0;
  discovery_response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));

  // Message's TypeUrl != Resource's
  stream->sendGrpcMessage(discovery_response);
  waitforFailureCount(1);
  EXPECT_THAT(TestXdsConfigTracker::ErrorMessage,
              HasSubstr("does not match the message-wide type URL"));
}

} // namespace
} // namespace Envoy
