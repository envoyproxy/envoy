#include <atomic>

#include "envoy/config/xds_resources_delegate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/xds_delegate_test_config.pb.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// A test implementation of the XdsResourcesDelegate extension.
class TestXdsResourcesDelegate : public Config::XdsResourcesDelegate {
public:
  void onConfigUpdated(const std::string& /*authority_id*/,
                       const std::string& /*resource_type_url*/,
                       const std::vector<Config::DecodedResourceRef>& /*resources*/) override {
    OnConfigUpdatedCount++;
  }

  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const std::string& /*authority_id*/,
               const std::string& /*resource_type_url*/) override {
    // TODO(abeyad): implement this and test for it when we add support for loading config from the
    // delegate in a subsequent PR.
    return {};
  }

  static int getOnConfigUpdatedCount() { return OnConfigUpdatedCount; }

private:
  static std::atomic<int> OnConfigUpdatedCount;
};
std::atomic<int> TestXdsResourcesDelegate::OnConfigUpdatedCount{0};

// A factory for creating the TestXdsResourcesDelegate test implementation.
class TestXdsResourcesDelegateFactory : public Config::XdsResourcesDelegateFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::envoy::config::xds::TestXdsResourcesDelegateConfig>();
  }

  std::string name() const override { return "envoy.config.xds.test_delegate"; };

  Config::XdsResourcesDelegatePtr createXdsResourcesDelegate(const ProtobufWkt::Any&,
                                                             ProtobufMessage::ValidationVisitor&,
                                                             Api::Api&) override {
    return std::make_unique<TestXdsResourcesDelegate>();
  }
};

std::string bootstrapConfig() {
  return fmt::format(R"EOF(
static_resources:
  clusters:
  - name: dummy_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: dummy_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: rtds_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
    load_assignment:
      cluster_name: rtds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
layered_runtime:
  layers:
  - name: some_static_layer
    static_layer:
      foo: whatevs
      bar: yar
  - name: some_rtds_layer
    rtds_layer:
      name: some_rtds_layer
      rtds_config:
        resource_api_version: V3
        api_config_source:
          api_type: GRPC
          transport_api_version: V3
          grpc_services:
            envoy_grpc:
              cluster_name: rtds_cluster
          set_node_on_first_message_only: true
  - name: some_admin_layer
    admin_layer: {{}}
xds_delegate_extension:
  name: envoy.config.xds.test_delegate
  typed_config:
    "@type": type.googleapis.com/test.envoy.config.xds.TestXdsResourcesDelegateConfig
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     Platform::null_device_path);
}

class XdsDelegateExtensionIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                            public HttpIntegrationTest {
public:
  XdsDelegateExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(), bootstrapConfig()) {
    // TODO(abeyad): Add test for Unified SotW when the UnifiedMux support is implemented.
    use_lds_ = false;
    create_xds_upstream_ = true;
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
    // Register admin port.
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

  void waitforOnConfigUpdatedCount(const int expected_count) {
    absl::MutexLock l(&lock_);
    const auto reached_expected_count = [expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return TestXdsResourcesDelegate::getOnConfigUpdatedCount() == expected_count;
    };
    timeSystem().waitFor(lock_, absl::Condition(&reached_expected_count),
                         TestUtility::DefaultTimeout);
  }

  absl::Mutex lock_;
  uint32_t initial_load_success_{0};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, XdsDelegateExtensionIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(XdsDelegateExtensionIntegrationTest, XdsResourcesDelegateOnConfigUpdated) {
  TestXdsResourcesDelegateFactory factory;
  Registry::InjectFactory<Config::XdsResourcesDelegateFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  int current_on_config_updated_count = TestXdsResourcesDelegate::getOnConfigUpdatedCount();
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
  int expected_on_config_updated_count = ++current_on_config_updated_count;
  waitforOnConfigUpdatedCount(expected_on_config_updated_count);

  EXPECT_EQ(expected_on_config_updated_count, TestXdsResourcesDelegate::getOnConfigUpdatedCount());
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().Runtime, "1", {"some_rtds_layer"}, {}, {}));
  some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      baz: saz
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "2");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 2);
  expected_on_config_updated_count = ++current_on_config_updated_count;
  waitforOnConfigUpdatedCount(expected_on_config_updated_count);

  EXPECT_EQ(expected_on_config_updated_count, TestXdsResourcesDelegate::getOnConfigUpdatedCount());
  EXPECT_EQ("saz", getRuntimeKey("baz"));
}

} // namespace
} // namespace Envoy
