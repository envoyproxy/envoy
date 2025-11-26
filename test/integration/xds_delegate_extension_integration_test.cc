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

#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr char XDS_CLUSTER_NAME[] = "xds_cluster";

// A test implementation of the XdsResourcesDelegate extension. It just saves and retrieves the xDS
// resources in a map.
class TestXdsResourcesDelegate : public Config::XdsResourcesDelegate {
public:
  TestXdsResourcesDelegate() {
    OnConfigUpdatedCount = 0;
    ResourcesMap.clear();
  }

  void onConfigUpdated(const Config::XdsSourceId& source_id,
                       const std::vector<Config::DecodedResourceRef>& resources) override {
    ++OnConfigUpdatedCount;
    for (const auto& resource_ref : resources) {
      const auto& decoded_resource = resource_ref.get();
      if (decoded_resource.hasResource()) {
        envoy::service::discovery::v3::Resource r;
        r.set_name(decoded_resource.name());
        r.set_version(decoded_resource.version());
        r.mutable_resource()->PackFrom(decoded_resource.resource());
        ResourcesMap[makeKey(source_id, decoded_resource.name())] = std::move(r);
      }
    }
  }

  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const Config::XdsSourceId& source_id,
               const absl::flat_hash_set<std::string>& resource_names) const override {
    std::vector<envoy::service::discovery::v3::Resource> resources;
    for (const auto& resource_name : resource_names) {
      auto it = ResourcesMap.find(makeKey(source_id, resource_name));
      if (it != ResourcesMap.end()) {
        resources.push_back(it->second);
      }
    }
    return resources;
  }

  void onResourceLoadFailed(const Config::XdsSourceId& /*source_id*/,
                            const std::string& /*resource_name*/,
                            const absl::optional<EnvoyException>& /*exception*/) override {}

  static std::atomic<int> OnConfigUpdatedCount;
  static std::map<std::string, envoy::service::discovery::v3::Resource> ResourcesMap;

private:
  std::string makeKey(const Config::XdsSourceId& source_id,
                      const std::string& resource_name) const {
    static constexpr char DELIMITER[] = "+";
    return absl::StrCat(source_id.toKey(), DELIMITER, resource_name);
  }
};
std::atomic<int> TestXdsResourcesDelegate::OnConfigUpdatedCount;
std::map<std::string, envoy::service::discovery::v3::Resource>
    TestXdsResourcesDelegate::ResourcesMap;

// A factory for creating the TestXdsResourcesDelegate test implementation.
class TestXdsResourcesDelegateFactory : public Config::XdsResourcesDelegateFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::envoy::config::xds::TestXdsResourcesDelegateConfig>();
  }

  std::string name() const override { return "envoy.config.xds.test_delegate"; };

  Config::XdsResourcesDelegatePtr createXdsResourcesDelegate(const Protobuf::Any&,
                                                             ProtobufMessage::ValidationVisitor&,
                                                             Api::Api&,
                                                             Event::Dispatcher&) override {
    return std::make_unique<TestXdsResourcesDelegate>();
  }
};

class XdsDelegateExtensionIntegrationTest : public Grpc::UnifiedOrLegacyMuxIntegrationParamTest,
                                            public HttpIntegrationTest {
public:
  XdsDelegateExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::baseConfigNoListeners()) {
    // TODO(abeyad): Add test for Unified SotW when the UnifiedMux support is implemented.
    use_lds_ = false;
    create_xds_upstream_ = true;

    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      isUnified() ? "true" : "false");

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
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
      layer->set_name("some_rtds_layer");
      auto* rtds_layer = layer->mutable_rtds_layer();
      rtds_layer->set_name("some_rtds_layer");
      auto* rtds_config = rtds_layer->mutable_rtds_config();
      rtds_config->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = rtds_config->mutable_api_config_source();
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_set_node_on_first_message_only(true);
      api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
          XDS_CLUSTER_NAME);
    });

    // Add test xDS delegate.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* delegate_extension = bootstrap.mutable_xds_delegate_extension();
      delegate_extension->set_name("envoy.config.xds.test_delegate");
      delegate_extension->mutable_typed_config()->PackFrom(
          test::envoy::config::xds::TestXdsResourcesDelegateConfig());
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
    auto entries = loader->getObject("entries").value();
    if (entries->hasObject(key)) {
      return entries->getObject(key).value()->getString("final_value").value();
    }
    return "";
  }

  void waitforOnConfigUpdatedCount(const int expected_count) {
    absl::MutexLock l(lock_);
    const auto reached_expected_count = [expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return TestXdsResourcesDelegate::OnConfigUpdatedCount == expected_count;
    };
    timeSystem().waitFor(lock_, absl::Condition(&reached_expected_count),
                         TestUtility::DefaultTimeout);
  }

  absl::Mutex lock_;
  uint32_t initial_load_success_{0};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, XdsDelegateExtensionIntegrationTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

// Verifies that, if an XdsResourcesDelegate is configured, then it is invoked whenever there is a
// set of updated resources in the DiscoveryResponse from an xDS server.
TEST_P(XdsDelegateExtensionIntegrationTest, XdsResourcesDelegateOnConfigUpdated) {
  TestXdsResourcesDelegateFactory factory;
  Registry::InjectFactory<Config::XdsResourcesDelegateFactory> registered(factory);

  initialize();
  acceptXdsConnection();

  int current_on_config_updated_count = TestXdsResourcesDelegate::OnConfigUpdatedCount;
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "", {"some_rtds_layer"},
                                      {"some_rtds_layer"}, {}, true));
  auto some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      foo: bar
      baz: meh
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "1");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 1);
  int expected_on_config_updated_count = ++current_on_config_updated_count;
  waitforOnConfigUpdatedCount(expected_on_config_updated_count);

  EXPECT_EQ(expected_on_config_updated_count, TestXdsResourcesDelegate::OnConfigUpdatedCount);
  EXPECT_EQ("bar", getRuntimeKey("foo"));
  EXPECT_EQ("meh", getRuntimeKey("baz"));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Runtime, "1", {"some_rtds_layer"},
                                      {}, {}));
  some_rtds_layer = TestUtility::parseYaml<envoy::service::runtime::v3::Runtime>(R"EOF(
    name: some_rtds_layer
    layer:
      baz: saz
  )EOF");
  sendDiscoveryResponse<envoy::service::runtime::v3::Runtime>(
      Config::TestTypeUrl::get().Runtime, {some_rtds_layer}, {some_rtds_layer}, {}, "2");
  test_server_->waitForCounterGe("runtime.load_success", initial_load_success_ + 2);
  expected_on_config_updated_count = ++current_on_config_updated_count;
  waitforOnConfigUpdatedCount(expected_on_config_updated_count);

  EXPECT_EQ(expected_on_config_updated_count, TestXdsResourcesDelegate::OnConfigUpdatedCount);
  EXPECT_EQ("saz", getRuntimeKey("baz"));
  ASSERT_EQ(TestXdsResourcesDelegate::ResourcesMap.size(), 1);
  envoy::service::runtime::v3::Runtime retrieved_rtds_layer;
  TestXdsResourcesDelegate::ResourcesMap
      ["xds_cluster+type.googleapis.com/envoy.service.runtime.v3.Runtime+some_rtds_layer"]
          .resource()
          .UnpackTo(&retrieved_rtds_layer);
  EXPECT_TRUE(TestUtility::protoEqual(retrieved_rtds_layer, some_rtds_layer));
}

} // namespace
} // namespace Envoy
